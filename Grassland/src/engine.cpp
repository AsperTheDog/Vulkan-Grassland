#include "engine.hpp"

#include <array>

#include <imgui.h>
#include <iostream>
#include <backends/imgui_impl_vulkan.h>

#include "camera.hpp"

#include "vertex.hpp"
#include "vulkan_binding.hpp"
#include "vulkan_buffer.hpp"

#include "vulkan_device.hpp"
#include "vulkan_render_pass.hpp"
#include "vulkan_sync.hpp"
#include "ext/vulkan_swapchain.hpp"
#include "vulkan_descriptors.hpp"
#include "utils/logger.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static VulkanGPU chooseCorrectGPU()
{
    std::array<VulkanGPU, 10> l_GPUs;
    VulkanContext::getGPUs(l_GPUs.data());

    VulkanGPU l_Selected{};
    for (uint32_t i = 0; i < VulkanContext::getGPUCount(); i++)
    {
        const VkPhysicalDeviceFeatures l_Features = l_GPUs[i].getFeatures();

        if (!l_Features.tessellationShader)
        {
            continue;
        }

        l_Selected = l_GPUs[i];
    }

    if (l_Selected.getHandle() != VK_NULL_HANDLE)
        return l_Selected;
    
    throw std::runtime_error("No discrete GPU found");
}

Engine::Engine()
    : m_Window("Vulkan", 1920, 1080), m_Camera(glm::vec3{0.f, 0.f, 5.f}, glm::vec3{0.f, 0.f, -1.f})
{
    // Vulkan Instance
    Logger::setRootContext("Engine init");

    std::vector<const char*> l_RequiredExtensions{ m_Window.getRequiredVulkanExtensionCount() };
    m_Window.getRequiredVulkanExtensions(l_RequiredExtensions.data());
#ifndef _DEBUG
    Logger::setLevels(Logger::WARN | Logger::ERR);
    VulkanContext::init(VK_API_VERSION_1_3, false, false, l_RequiredExtensions);
#else
    Logger::setLevels(Logger::ALL);
    VulkanContext::init(VK_API_VERSION_1_3, true, true, l_RequiredExtensions);
#endif

    VulkanContext::initializeTransientMemory(1LL * 1024);
    VulkanContext::initializeArenaMemory(1LL * 1024 * 1024);

    // Vulkan Surface
    m_Window.createSurface(VulkanContext::getHandle());

    // Choose Physical Device
    const VulkanGPU l_GPU = chooseCorrectGPU();

    // Select Queue Families
    const GPUQueueStructure l_QueueStructure = l_GPU.getQueueFamilies();
    QueueFamilySelector l_QueueFamilySelector(l_QueueStructure);

    const QueueFamily l_GraphicsQueueFamily = l_QueueStructure.findQueueFamily(VK_QUEUE_GRAPHICS_BIT);
    const QueueFamily l_PresentQueueFamily = l_QueueStructure.findPresentQueueFamily(m_Window.getSurface());
    const QueueFamily l_TransferQueueFamily = l_QueueStructure.findQueueFamily(VK_QUEUE_TRANSFER_BIT);

    // Select Queue Families and assign queues
    QueueFamilySelector l_Selector{ l_QueueStructure };
    l_Selector.selectQueueFamily(l_GraphicsQueueFamily, QueueFamilyTypeBits::GRAPHICS);
    l_Selector.selectQueueFamily(l_PresentQueueFamily, QueueFamilyTypeBits::PRESENT);
    m_GraphicsQueuePos = l_Selector.getOrAddQueue(l_GraphicsQueueFamily, 1.0);
    m_PresentQueuePos = l_Selector.getOrAddQueue(l_PresentQueueFamily, 1.0);
    m_TransferQueuePos = l_Selector.addQueue(l_TransferQueueFamily, 1.0);

    // Logical Device
    VulkanDeviceExtensionManager l_Extensions{};
    l_Extensions.addExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME, new VulkanSwapchainExtension(m_DeviceID));
    m_DeviceID = VulkanContext::createDevice(l_GPU, l_Selector, &l_Extensions, {.tessellationShader = true, .fillModeNonSolid = true});
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);

    // Swapchain
    VulkanSwapchainExtension* l_SwapchainExt = VulkanSwapchainExtension::get(m_DeviceID);
    m_SwapchainID = l_SwapchainExt->createSwapchain(m_Window.getSurface(), m_Window.getSize().toExtent2D(), { VK_FORMAT_R8G8B8A8_SRGB, VK_COLORSPACE_SRGB_NONLINEAR_KHR });
    VulkanSwapchain& l_Swapchain = l_SwapchainExt->getSwapchain(m_SwapchainID);

    // Command Buffers
    l_Device.configureOneTimeQueue(m_TransferQueuePos);
    l_Device.initializeCommandPool(l_GraphicsQueueFamily, 0, true);
    m_GraphicsCmdBufferID = l_Device.createCommandBuffer(l_GraphicsQueueFamily, 0, false);

    // Depth Buffer
    m_DepthBuffer = l_Device.createImage(VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT, { l_Swapchain.getExtent().width, l_Swapchain.getExtent().height, 1 }, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0);
    VulkanImage& l_DepthImage = l_Device.getImage(m_DepthBuffer);
    l_DepthImage.allocateFromFlags({ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false });
    m_DepthBufferView = l_DepthImage.createImageView(VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

    l_Device.configureStagingBuffer(100LL * 1024 * 1024, m_TransferQueuePos);

    // Renderpass and pipelines
    createHeightmapDescriptor();
    createRenderPasses();
    createPipelines();

    // Framebuffers
    m_FramebufferIDs.resize(l_Swapchain.getImageCount());
    for (uint32_t i = 0; i < l_Swapchain.getImageCount(); i++)
    {
        VkImageView l_Color = *l_Swapchain.getImage(i).getImageView(l_Swapchain.getImageView(i));
        const std::array<VkImageView, 2> l_Attachments = { l_Color, *l_Device.getImage(m_DepthBuffer).getImageView(m_DepthBufferView) };
        m_FramebufferIDs[i] = VulkanContext::getDevice(m_DeviceID).createFramebuffer({ l_Swapchain.getExtent().width, l_Swapchain.getExtent().height, 1 }, m_RenderPassID, l_Attachments);
    }

    // Sync objects
    m_RenderFinishedSemaphoreID = l_Device.createSemaphore();
    m_InFlightFenceID = l_Device.createFence(true);

    m_Window.getMouseMovedSignal().connect(&m_Camera, &Camera::mouseMoved);
    m_Window.getKeyPressedSignal().connect(&m_Camera, &Camera::keyPressed);
    m_Window.getKeyReleasedSignal().connect(&m_Camera, &Camera::keyReleased);
    m_Window.getEventsProcessedSignal().connect(&m_Camera, &Camera::updateEvents);
    m_Window.getMouseCaptureChangedSignal().connect(&m_Camera, &Camera::setMouseCaptured);
    m_Window.getResizedSignal().connect(this, &Engine::recreateSwapchain);

    initImgui();

    m_Window.toggleMouseCapture();
}

Engine::~Engine()
{
    VulkanContext::getDevice(m_DeviceID).waitIdle();

    Logger::setRootContext("Resource cleanup");

    ImGui_ImplVulkan_Shutdown();
    m_Window.shutdownImgui();
    ImGui::DestroyContext();

    VulkanContext::freeDevice(m_DeviceID);
    m_Window.free();
    VulkanContext::free();
}

void Engine::run()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    VulkanSwapchainExtension* l_SwapchainExt = VulkanSwapchainExtension::get(l_Device);

    VulkanFence& l_InFlightFence = l_Device.getFence(m_InFlightFenceID);

    const VulkanQueue l_GraphicsQueue = l_Device.getQueue(m_GraphicsQueuePos);
    VulkanCommandBuffer& l_GraphicsBuffer = l_Device.getCommandBuffer(m_GraphicsCmdBufferID, 0);

    while (!m_Window.shouldClose())
    {
        m_Window.pollEvents();
        if (m_Window.isMinimized())
        {
            continue;
        }

        l_InFlightFence.wait();
        l_InFlightFence.reset();

        VulkanSwapchain& l_Swapchain = l_SwapchainExt->getSwapchain(m_SwapchainID);
        const uint32_t l_ImageIndex = l_Swapchain.acquireNextImage();
        if (l_ImageIndex == UINT32_MAX)
        {
            continue;
        }

        Engine::drawImgui();
        ImDrawData* l_ImguiDrawData = ImGui::GetDrawData();

        if (l_ImguiDrawData->DisplaySize.x <= 0.0f || l_ImguiDrawData->DisplaySize.y <= 0.0f)
        {
            continue;
        }

        // Record
        {
            l_GraphicsBuffer.reset();
            render(l_GraphicsBuffer, l_ImageIndex, l_ImguiDrawData);
        }

        // Submit
        {
            const std::array<VulkanCommandBuffer::WaitSemaphoreData, 1> l_WaitSemaphores = {{{l_Swapchain.getImgSemaphore(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT}}};
            const std::array<ResourceID, 1> l_SignalSemaphores = {m_RenderFinishedSemaphoreID};
            l_GraphicsBuffer.submit(l_GraphicsQueue, l_WaitSemaphores, l_SignalSemaphores, m_InFlightFenceID);
        }

        // Present
        {
            std::array<ResourceID, 1> l_Semaphores = { {m_RenderFinishedSemaphoreID} };
            l_Swapchain.present(m_PresentQueuePos, l_Semaphores);
        }

        VulkanContext::resetTransMemory();
    }
}

void Engine::createRenderPasses()
{
    Logger::pushContext("Create RenderPass");
    VulkanRenderPassBuilder l_Builder{};
    
    VulkanSwapchainExtension* l_SwapchainExt = VulkanSwapchainExtension::get(m_DeviceID);
    const VkFormat l_Format = l_SwapchainExt->getSwapchain(m_SwapchainID).getFormat().format;

    const VkAttachmentDescription l_ColorAttachment = VulkanRenderPassBuilder::createAttachment(l_Format,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    l_Builder.addAttachment(l_ColorAttachment);
    const VkAttachmentDescription l_DepthAttachment = VulkanRenderPassBuilder::createAttachment(VK_FORMAT_D32_SFLOAT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    l_Builder.addAttachment(l_DepthAttachment);

    const std::array<VulkanRenderPassBuilder::AttachmentReference, 2> l_ColorReferences = {{{COLOR, 0}, {DEPTH_STENCIL, 1}}};
    l_Builder.addSubpass(VK_PIPELINE_BIND_POINT_GRAPHICS, l_ColorReferences, 0);

    m_RenderPassID = VulkanContext::getDevice(m_DeviceID).createRenderPass(l_Builder, 0);
    Logger::popContext();
}

void Engine::createPipelines()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);

    std::array<VkPushConstantRange, 3> l_PushConstantRanges;
    l_PushConstantRanges[0] = { VK_SHADER_STAGE_VERTEX_BIT, PushConstantData::getVertexShaderOffset(), PushConstantData::getVertexShaderSize() };
    l_PushConstantRanges[1] = { VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, PushConstantData::getTessellationControlShaderOffset(), PushConstantData::getTessellationControlShaderSize() };
    l_PushConstantRanges[2] = { VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, PushConstantData::getTessellationEvaluationShaderOffset(), PushConstantData::getTessellationEvaluationShaderSize() };
    std::array<ResourceID, 1> l_DescriptorSetLayouts = { m_TessellationDescriptorSetLayoutID };
    m_TessellationPipelineLayoutID = l_Device.createPipelineLayout(l_DescriptorSetLayouts, l_PushConstantRanges);

    VkPipelineColorBlendAttachmentState l_ColorBlendAttachment;
    l_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    l_ColorBlendAttachment.blendEnable = VK_FALSE;
    l_ColorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    l_ColorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    l_ColorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    l_ColorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    l_ColorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    l_ColorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    std::array<VkDynamicState, 2> l_DynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    const uint32_t vertexShaderID = l_Device.createShader("shaders/shader.vert", VK_SHADER_STAGE_VERTEX_BIT, false, {});
    const uint32_t fragmentShaderID = l_Device.createShader("shaders/shader.frag", VK_SHADER_STAGE_FRAGMENT_BIT, false, {});
    const uint32_t tessellationControlShaderID = l_Device.createShader("shaders/shader.tesc", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, false, {});
    const uint32_t tessellationEvaluationShaderID = l_Device.createShader("shaders/shader.tese", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, false, {});

    VulkanPipelineBuilder l_TessellationBuilder{m_DeviceID};
    l_TessellationBuilder.setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST, VK_FALSE);
    l_TessellationBuilder.setTessellationState(4);
    l_TessellationBuilder.setViewportState(1, 1);
    l_TessellationBuilder.setRasterizationState(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    l_TessellationBuilder.setMultisampleState(VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 1.0f);
    l_TessellationBuilder.setDepthStencilState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);
    l_TessellationBuilder.addColorBlendAttachment(l_ColorBlendAttachment);
    l_TessellationBuilder.setColorBlendState(VK_FALSE, VK_LOGIC_OP_COPY, { 0.0f, 0.0f, 0.0f, 0.0f });
    l_TessellationBuilder.setDynamicState(l_DynamicStates);
    l_TessellationBuilder.addShaderStage(vertexShaderID, "main");
    l_TessellationBuilder.addShaderStage(fragmentShaderID, "main");
    l_TessellationBuilder.addShaderStage(tessellationControlShaderID, "main");
    l_TessellationBuilder.addShaderStage(tessellationEvaluationShaderID, "main");

    m_TessellationPipelineID = l_Device.createPipeline(l_TessellationBuilder, m_TessellationPipelineLayoutID, m_RenderPassID, 0);
}

void Engine::createHeightmapDescriptor()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);

    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load("assets/perlin.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels)
    {
        throw std::runtime_error("Failed to load texture image!");
    }
    const VkExtent3D extent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };

    m_HeightmapID = l_Device.createImage(VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0);
    VulkanImage& l_HeightmapImage = l_Device.getImage(m_HeightmapID);
    l_HeightmapImage.allocateFromFlags({ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false });
    l_Device.dumpDataIntoImage(m_HeightmapID, pixels, extent, 4, 0, false);
    stbi_image_free(pixels);

    VulkanMemoryBarrierBuilder l_BarrierBuilder{m_DeviceID, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, 0};
    l_BarrierBuilder.addImageMemoryBarrier(m_HeightmapID, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_GraphicsQueuePos.familyIndex);

    const ResourceID l_BufferID = l_Device.createOneTimeCommandBuffer(0);
    VulkanCommandBuffer& l_Buffer = l_Device.getCommandBuffer(l_BufferID, 0);
    l_Buffer.beginRecording(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    l_Buffer.cmdPipelineBarrier(l_BarrierBuilder);
    l_Buffer.endRecording();
    l_Buffer.submit(l_Device.getQueue(m_TransferQueuePos), {}, {});

    m_HeightmapViewID = l_HeightmapImage.createImageView(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    m_HeightmapSamplerID = l_HeightmapImage.createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    {
        VkDescriptorSetLayoutBinding l_Binding{};
        l_Binding.binding = 0;
        l_Binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_Binding.descriptorCount = 1;
        l_Binding.stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

        std::array<VkDescriptorSetLayoutBinding, 1> l_Bindings = { l_Binding };
        m_TessellationDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
    }

    std::array<VkDescriptorPoolSize, 1> l_PoolSizes = {{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}};
    m_TessellationDescriptorPoolID = l_Device.createDescriptorPool(l_PoolSizes, 1, 0);
    m_TessellationDescriptorSetID = l_Device.createDescriptorSet(m_TessellationDescriptorPoolID, m_TessellationDescriptorSetLayoutID);

    VkDescriptorImageInfo l_ImageInfo;
    l_ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    l_ImageInfo.imageView = *l_HeightmapImage.getImageView(m_HeightmapViewID);
    l_ImageInfo.sampler = *l_HeightmapImage.getSampler(m_HeightmapSamplerID);

    std::array<VkWriteDescriptorSet, 1> l_Writes{};
    l_Writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    l_Writes[0].dstSet = *l_Device.getDescriptorSet(m_TessellationDescriptorSetID);
    l_Writes[0].dstBinding = 0;
    l_Writes[0].dstArrayElement = 0;
    l_Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    l_Writes[0].descriptorCount = 1;
    l_Writes[0].pImageInfo = &l_ImageInfo;

    l_Device.updateDescriptorSets(l_Writes);

    l_Device.waitIdle();
    l_Device.freeCommandBuffer(l_BufferID, 0);
}

void Engine::render(VulkanCommandBuffer& p_Buffer, const uint32_t l_ImageIndex, ImDrawData* p_ImGuiDrawData)
{
    m_PushConstants.cameraPos = m_Camera.getPosition();
    m_PushConstants.mvp = m_Camera.getVPMatrix();

    const VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    VulkanSwapchainExtension* l_SwapchainExt = VulkanSwapchainExtension::get(l_Device);

    const VkExtent2D& extent = l_SwapchainExt->getSwapchain(m_SwapchainID).getExtent();

    std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset = { 0, 0 };
    scissor.extent = extent;

    p_Buffer.beginRecording();

    p_Buffer.cmdBeginRenderPass(m_RenderPassID, m_FramebufferIDs[l_ImageIndex], extent, clearValues);
    p_Buffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, m_TessellationPipelineID);
    p_Buffer.cmdSetViewport(viewport);
    p_Buffer.cmdSetScissor(scissor);

    p_Buffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, m_TessellationPipelineLayoutID, m_TessellationDescriptorSetID);
    p_Buffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_VERTEX_BIT, PushConstantData::getVertexShaderOffset(), PushConstantData::getVertexShaderSize(), &m_PushConstants);
    p_Buffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, PushConstantData::getTessellationControlShaderOffset(), PushConstantData::getTessellationControlShaderSize(), &m_PushConstants.minTessLevel);
    p_Buffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, PushConstantData::getTessellationEvaluationShaderOffset(), PushConstantData::getTessellationEvaluationShaderSize(), &m_PushConstants.heightScale);

    p_Buffer.cmdDraw(m_PushConstants.gridSize * m_PushConstants.gridSize * 4, 0);

    ImGui_ImplVulkan_RenderDrawData(p_ImGuiDrawData, *p_Buffer);

    p_Buffer.cmdEndRenderPass();
    p_Buffer.endRecording();
}

void Engine::recreateSwapchain(const VkExtent2D p_NewSize)
{
    Logger::pushContext("Recreate Swapchain");
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    l_Device.waitIdle();

    VulkanSwapchainExtension* swapchainExtension = VulkanSwapchainExtension::get(l_Device);

    m_SwapchainID = swapchainExtension->createSwapchain(m_Window.getSurface(), p_NewSize, swapchainExtension->getSwapchain(m_SwapchainID).getFormat(), m_SwapchainID);

    VulkanSwapchain& l_Swapchain = swapchainExtension->getSwapchain(m_SwapchainID);

    for (uint32_t i = 0; i < l_Swapchain.getImageCount(); ++i)
    {
        const VkImageView l_Color = *l_Swapchain.getImage(i).getImageView(l_Swapchain.getImageView(i));
        const std::array<VkImageView, 2> l_Attachments = { l_Color, *l_Device.getImage(m_DepthBuffer).getImageView(m_DepthBufferView) };
        m_FramebufferIDs[i] = VulkanContext::getDevice(m_DeviceID).createFramebuffer({ l_Swapchain.getExtent().width, l_Swapchain.getExtent().height, 1 }, m_RenderPassID, l_Attachments);
    }
    Logger::popContext();
}

void Engine::initImgui() const
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    const ImGuiIO& l_IO = ImGui::GetIO(); (void)l_IO;

    ImGui::StyleColorsDark();

    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);

    const std::array<VkDescriptorPoolSize, 11> l_PoolSizes =
    {{
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    }};
    const uint32_t l_ImguiPoolID = l_Device.createDescriptorPool(l_PoolSizes, 1000U * static_cast<uint32_t>(l_PoolSizes.size()), VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

    VulkanSwapchainExtension* l_SwapchainExt = VulkanSwapchainExtension::get(l_Device);
    const VulkanSwapchain& l_Swapchain = l_SwapchainExt->getSwapchain(m_SwapchainID);

    m_Window.initImgui();
    ImGui_ImplVulkan_InitInfo l_InitInfo = {};
    l_InitInfo.Instance = VulkanContext::getHandle();
    l_InitInfo.PhysicalDevice = *l_Device.getGPU();
    l_InitInfo.Device = *l_Device;
    l_InitInfo.QueueFamily = m_GraphicsQueuePos.familyIndex;
    l_InitInfo.Queue = *l_Device.getQueue(m_GraphicsQueuePos);
    l_InitInfo.DescriptorPool = *l_Device.getDescriptorPool(l_ImguiPoolID);
    l_InitInfo.RenderPass = *l_Device.getRenderPass(m_RenderPassID);
    l_InitInfo.Subpass = 0;
    l_InitInfo.MinImageCount = l_Swapchain.getMinImageCount();
    l_InitInfo.ImageCount = l_Swapchain.getImageCount();
    l_InitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&l_InitInfo );
}

void Engine::drawImgui()
{
    ImGui_ImplVulkan_NewFrame();
    m_Window.frameImgui();
    ImGui::NewFrame();

    // ImGui here
    {
        ImGui::Begin("Controls");

        ImGui::DragFloat("Height scale", &m_PushConstants.heightScale, 0.1f);
        ImGui::Separator();
        int l_GridSize = m_PushConstants.gridSize;
        ImGui::DragInt("Grid size", &l_GridSize, 1, 1, 100);
        m_PushConstants.gridSize = static_cast<uint32_t>(l_GridSize);
        ImGui::Separator();
        ImGui::DragFloat("Tessellation min", &m_PushConstants.minTessLevel, 0.1f, 1.f, 64.f);
        if (m_PushConstants.minTessLevel > m_PushConstants.maxTessLevel)
            m_PushConstants.minTessLevel = m_PushConstants.maxTessLevel;
        ImGui::DragFloat("Tessellation max", &m_PushConstants.maxTessLevel, 0.1f, 1.f, 64.f);
        if (m_PushConstants.minTessLevel > m_PushConstants.maxTessLevel)
            m_PushConstants.maxTessLevel = m_PushConstants.minTessLevel;
        ImGui::DragFloat("Tessellation factor", &m_PushConstants.tessFactor, 0.001f, 0.01f, 1.f);
        ImGui::DragFloat("Tessellation slope", &m_PushConstants.tessSlope, 0.01f, 0.01f, 2.f);
        ImGui::End();
    }

    ImGui::Render();
}
