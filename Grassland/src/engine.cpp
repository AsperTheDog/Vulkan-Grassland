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
    const QueueFamily l_ComputeQueueFamily = l_QueueStructure.findQueueFamily(VK_QUEUE_COMPUTE_BIT);
    const QueueFamily l_PresentQueueFamily = l_QueueStructure.findPresentQueueFamily(m_Window.getSurface());
    const QueueFamily l_TransferQueueFamily = l_QueueStructure.findQueueFamily(VK_QUEUE_TRANSFER_BIT);

    // Select Queue Families and assign queues
    QueueFamilySelector l_Selector{ l_QueueStructure };
    l_Selector.selectQueueFamily(l_GraphicsQueueFamily, QueueFamilyTypeBits::GRAPHICS);
    l_Selector.selectQueueFamily(l_PresentQueueFamily, QueueFamilyTypeBits::PRESENT);
    m_GraphicsQueuePos = l_Selector.getOrAddQueue(l_GraphicsQueueFamily, 1.0);
    m_ComputeQueuePos = l_Selector.getOrAddQueue(l_ComputeQueueFamily, 1.0);
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
    if (m_GraphicsQueuePos != m_ComputeQueuePos)
        m_ComputeCmdBufferID = l_Device.createCommandBuffer(l_ComputeQueueFamily, 0, false);
    else
    {
        m_ComputeCmdBufferID = m_GraphicsCmdBufferID;
        m_UsingSharedCmdBuffer = true;
    }

    // Depth Buffer
    m_DepthBuffer = l_Device.createImage(VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT, { l_Swapchain.getExtent().width, l_Swapchain.getExtent().height, 1 }, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0);
    VulkanImage& l_DepthImage = l_Device.getImage(m_DepthBuffer);
    l_DepthImage.allocateFromFlags({ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false });
    m_DepthBufferView = l_DepthImage.createImageView(VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

    l_Device.configureStagingBuffer(100LL * 1024 * 1024, m_TransferQueuePos);

    //Descriptor pool
    std::array<VkDescriptorPoolSize, 2> l_PoolSizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}
    };
    m_DescriptorPoolID = l_Device.createDescriptorPool(l_PoolSizes, 2, 0);

    // Renderpass and pipelines
    createHeightmapDescriptor(512, 512);
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
    if (m_UsingSharedCmdBuffer)
        m_ComputeFinishedSemaphoreID = l_Device.createSemaphore();

    m_Window.getMouseMovedSignal().connect(&m_Camera, &Camera::mouseMoved);
    m_Window.getKeyPressedSignal().connect(&m_Camera, &Camera::keyPressed);
    m_Window.getKeyReleasedSignal().connect(&m_Camera, &Camera::keyReleased);
    m_Window.getEventsProcessedSignal().connect(&m_Camera, &Camera::updateEvents);
    m_Window.getMouseCaptureChangedSignal().connect(&m_Camera, &Camera::setMouseCaptured);
    m_Window.getResizedSignal().connect(this, &Engine::recreateSwapchain);
    m_Window.getMouseScrolledSignal().connect(&m_Camera, &Camera::mouseScrolled);

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
            const bool l_RenderedNoise = renderNoise();
            render(l_ImageIndex, l_ImguiDrawData, l_RenderedNoise);
        }

        // Submit
        {
            const std::array<ResourceID, 1> l_SignalSemaphores = { m_RenderFinishedSemaphoreID };
            if (m_UsingSharedCmdBuffer)
            {
                const std::array<VulkanCommandBuffer::WaitSemaphoreData, 1> l_WaitSemaphores = {
                    VulkanCommandBuffer::WaitSemaphoreData{l_Swapchain.getImgSemaphore(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT}
                };
                l_GraphicsBuffer.submit(l_GraphicsQueue, l_WaitSemaphores, l_SignalSemaphores, m_InFlightFenceID);
            }
            else
            {
                const std::array<VulkanCommandBuffer::WaitSemaphoreData, 2> l_WaitSemaphores = {
                    VulkanCommandBuffer::WaitSemaphoreData{l_Swapchain.getImgSemaphore(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
                    VulkanCommandBuffer::WaitSemaphoreData{m_ComputeFinishedSemaphoreID, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT}
                };
                l_GraphicsBuffer.submit(l_GraphicsQueue, l_WaitSemaphores, l_SignalSemaphores, m_InFlightFenceID);
            }
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

    VkSubpassDependency l_Dependency{};
    l_Dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    l_Dependency.dstSubpass = 0;
    l_Dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    l_Dependency.srcAccessMask = 0;
    l_Dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    l_Dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    l_Builder.addDependency(l_Dependency);

    m_RenderPassID = VulkanContext::getDevice(m_DeviceID).createRenderPass(l_Builder, 0);
    Logger::popContext();
}

void Engine::createPipelines()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);

    std::array<VkPushConstantRange, 4> l_PushConstantRanges;
    l_PushConstantRanges[0] = { VK_SHADER_STAGE_VERTEX_BIT, PushConstantData::getVertexShaderOffset(), PushConstantData::getVertexShaderSize() };
    l_PushConstantRanges[1] = { VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, PushConstantData::getTessellationControlShaderOffset(), PushConstantData::getTessellationControlShaderSize() };
    l_PushConstantRanges[2] = { VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, PushConstantData::getTessellationEvaluationShaderOffset(), PushConstantData::getTessellationEvaluationShaderSize() };
    l_PushConstantRanges[3] = { VK_SHADER_STAGE_FRAGMENT_BIT, PushConstantData::getFragmentShaderOffset(), PushConstantData::getFragmentShaderSize() };
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
    l_TessellationBuilder.setRasterizationState(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
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

    l_TessellationBuilder.setRasterizationState(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    m_TessellationPipelineWFID = l_Device.createPipeline(l_TessellationBuilder, m_TessellationPipelineLayoutID, m_RenderPassID, 0);

    l_Device.freeShader(vertexShaderID);
    l_Device.freeShader(fragmentShaderID);
    l_Device.freeShader(tessellationControlShaderID);
    l_Device.freeShader(tessellationEvaluationShaderID);

    std::array<VkPushConstantRange, 1> l_ComputePushConstantRanges;
    l_ComputePushConstantRanges[0] = { VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstantData)} };
    std::array<ResourceID, 1> l_ComputeDescriptorSetLayouts = { m_ComputeDescriptorSetLayoutID };
    m_ComputePipelineLayoutID = l_Device.createPipelineLayout(l_ComputeDescriptorSetLayouts, l_ComputePushConstantRanges);

    const uint32_t l_ComputeShaderID = l_Device.createShader("shaders/noise.comp", VK_SHADER_STAGE_COMPUTE_BIT, false, {});
    m_ComputeNoisePipelineID = l_Device.createComputePipeline(m_ComputePipelineLayoutID, l_ComputeShaderID, "main");
}

void Engine::createHeightmapDescriptor(const uint32_t p_TextWidth, const uint32_t p_TextHeight)
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);

    const VkExtent3D extent = { p_TextWidth, p_TextHeight, 1 };

    m_HeightmapID = l_Device.createImage(VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 0);
    VulkanImage& l_HeightmapImage = l_Device.getImage(m_HeightmapID);
    l_HeightmapImage.allocateFromFlags({ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false });
    l_HeightmapImage.setQueue(m_ComputeQueuePos.familyIndex);

    m_HeightmapViewID = l_HeightmapImage.createImageView(VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
    m_HeightmapSamplerID = l_HeightmapImage.createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    {
        std::array<VkDescriptorSetLayoutBinding, 1> l_Bindings;
        l_Bindings[0].binding = 0;
        l_Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_Bindings[0].descriptorCount = 1;
        l_Bindings[0].stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        l_Bindings[0].pImmutableSamplers = nullptr;

        m_TessellationDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
    }

    m_TessellationDescriptorSetID = l_Device.createDescriptorSet(m_DescriptorPoolID, m_TessellationDescriptorSetLayoutID);

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

    {
        std::array<VkDescriptorSetLayoutBinding, 1> l_Bindings;
        l_Bindings[0].binding = 0;
        l_Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        l_Bindings[0].descriptorCount = 1;
        l_Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        m_ComputeDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
    }

    m_ComputeNoiseDescriptorSetID = l_Device.createDescriptorSet(m_DescriptorPoolID, m_ComputeDescriptorSetLayoutID);

    VkDescriptorImageInfo l_ComputeImageInfo;
    l_ComputeImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    l_ComputeImageInfo.imageView = *l_HeightmapImage.getImageView(m_HeightmapViewID);
    l_ComputeImageInfo.sampler = *l_HeightmapImage.getSampler(m_HeightmapSamplerID);

    std::array<VkWriteDescriptorSet, 1> l_ComputeWrites{};
    l_ComputeWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    l_ComputeWrites[0].dstSet = *l_Device.getDescriptorSet(m_ComputeNoiseDescriptorSetID);
    l_ComputeWrites[0].dstBinding = 0;
    l_ComputeWrites[0].dstArrayElement = 0;
    l_ComputeWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    l_ComputeWrites[0].descriptorCount = 1;
    l_ComputeWrites[0].pImageInfo = &l_ComputeImageInfo;

    l_Device.updateDescriptorSets(l_ComputeWrites);
}

void Engine::render(const uint32_t l_ImageIndex, ImDrawData* p_ImGuiDrawData, const bool p_ComputedNoise)
{
    VulkanCommandBuffer& p_Buffer = VulkanContext::getDevice(m_DeviceID).getCommandBuffer(m_GraphicsCmdBufferID, 0);

    m_PushConstants.cameraPos = m_Camera.getPosition();
    m_PushConstants.mvp = m_Camera.getVPMatrix();

    const float l_Extent = m_PushConstants.patchSize * static_cast<float>(m_PushConstants.gridSize);
    m_PushConstants.uvOffsetScale = (m_UVOffset / 100.f) * l_Extent; 

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

    if (!m_UsingSharedCmdBuffer || !p_ComputedNoise)
        p_Buffer.beginRecording();

    p_Buffer.cmdBeginRenderPass(m_RenderPassID, m_FramebufferIDs[l_ImageIndex], extent, clearValues);
    p_Buffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, m_Wireframe ? m_TessellationPipelineWFID : m_TessellationPipelineID);
    p_Buffer.cmdSetViewport(viewport);
    p_Buffer.cmdSetScissor(scissor);

    p_Buffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, m_TessellationPipelineLayoutID, m_TessellationDescriptorSetID);
    p_Buffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_VERTEX_BIT, PushConstantData::getVertexShaderOffset(), PushConstantData::getVertexShaderSize(), &m_PushConstants);
    p_Buffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, PushConstantData::getTessellationControlShaderOffset(), PushConstantData::getTessellationControlShaderSize(), &m_PushConstants.minTessLevel);
    p_Buffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, PushConstantData::getTessellationEvaluationShaderOffset(), PushConstantData::getTessellationEvaluationShaderSize(), &m_PushConstants.heightScale);
    p_Buffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_FRAGMENT_BIT, PushConstantData::getFragmentShaderOffset(), PushConstantData::getFragmentShaderSize(), &m_PushConstants.color);

    p_Buffer.cmdDraw(m_PushConstants.gridSize * m_PushConstants.gridSize * 4, 0);

    ImGui_ImplVulkan_RenderDrawData(p_ImGuiDrawData, *p_Buffer);

    p_Buffer.cmdEndRenderPass();
    p_Buffer.endRecording();
}

bool Engine::renderNoise()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    VulkanCommandBuffer& l_Buffer = l_Device.getCommandBuffer(m_ComputeCmdBufferID, 0);

    const bool l_NeedsCompute = m_NoiseDirty || m_NormalDirty;

    if (l_NeedsCompute)
    {
        l_Buffer.reset();
        l_Buffer.beginRecording();
    }

    VulkanImage& l_HeightmapImage = l_Device.getImage(m_HeightmapID);
    const VkExtent3D l_ImageSize = l_HeightmapImage.getSize();
    const uint32_t groupCountX = (l_ImageSize.width + 7) / 8; // Adjust to local size
    const uint32_t groupCountY = (l_ImageSize.height + 7) / 8;

    if (m_NoiseDirty)
    {
        VulkanMemoryBarrierBuilder l_EnterBarrierBuilder{m_DeviceID, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
        l_EnterBarrierBuilder.addImageMemoryBarrier(m_HeightmapID, VK_IMAGE_LAYOUT_GENERAL, m_ComputeQueuePos.familyIndex, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
        l_Buffer.cmdPipelineBarrier(l_EnterBarrierBuilder);
        l_HeightmapImage.setLayout(VK_IMAGE_LAYOUT_GENERAL);
        l_HeightmapImage.setQueue(m_ComputeQueuePos.familyIndex);

        l_Buffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeNoisePipelineID);
        l_Buffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipelineLayoutID, m_ComputeNoiseDescriptorSetID);
        l_Buffer.cmdPushConstant(m_ComputePipelineLayoutID, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstantData), &m_ComputePushConstants);
        l_Buffer.cmdDispatch(groupCountX, groupCountY, 1);

        VulkanMemoryBarrierBuilder l_ExitBarrierBuilder{m_DeviceID, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, 0};
        l_ExitBarrierBuilder.addImageMemoryBarrier(m_HeightmapID, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_GraphicsQueuePos.familyIndex, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        l_Buffer.cmdPipelineBarrier(l_ExitBarrierBuilder);
        l_HeightmapImage.setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        l_HeightmapImage.setQueue(m_GraphicsQueuePos.familyIndex);

        m_NormalDirty = true;
        m_NoiseDirty = false;
    }

    if (m_NormalDirty)
    {
        /*VulkanMemoryBarrierBuilder l_EnterBarrierBuilder{m_DeviceID, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
        l_EnterBarrierBuilder.addImageMemoryBarrier(m_NormalmapID, VK_IMAGE_LAYOUT_GENERAL, m_ComputeQueuePos.familyIndex, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
        l_Buffer.cmdPipelineBarrier(l_EnterBarrierBuilder);

        VulkanMemoryBarrierBuilder l_ExitBarrierBuilder{m_DeviceID, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
        l_EnterBarrierBuilder.addImageMemoryBarrier(m_NormalmapID, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_GraphicsQueuePos.familyIndex, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        l_Buffer.cmdPipelineBarrier(l_ExitBarrierBuilder);*/

        m_NormalDirty = false;
    }

    if (l_NeedsCompute && !m_UsingSharedCmdBuffer)
    {
        l_Buffer.endRecording();

        const VulkanQueue l_ComputeQueue = l_Device.getQueue(m_ComputeQueuePos);
        const std::array<VulkanCommandBuffer::WaitSemaphoreData, 1> l_WaitSemaphores = { VulkanCommandBuffer::WaitSemaphoreData{m_RenderFinishedSemaphoreID, 0} };
        const std::array<ResourceID, 1> l_SignalSemaphores = { m_ComputeFinishedSemaphoreID };
        l_Buffer.submit(l_ComputeQueue, l_WaitSemaphores, l_SignalSemaphores);
    }

    return l_NeedsCompute;
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

void Engine::initImgui()
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
    ImGui_ImplVulkan_Init(&l_InitInfo);

    VulkanImage l_HeightmapImage = l_Device.getImage(m_HeightmapID);
    m_HeightmapDescriptorSet = ImGui_ImplVulkan_AddTexture(*l_HeightmapImage.getSampler(m_HeightmapSamplerID), *l_HeightmapImage.getImageView(m_HeightmapViewID), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Engine::drawImgui()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);

    ImGui_ImplVulkan_NewFrame();
    m_Window.frameImgui();
    ImGui::NewFrame();

    // ImGui here
    {
        ImGui::Begin("Performance");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();

        ImGui::Begin("Controls");

        ImGui::DragFloat("Height scale", &m_PushConstants.heightScale, 0.1f);
        ImGui::DragFloat("UV Offset Scale", &m_UVOffset, 0.001f, 0.001f, 1.f);
        ImGui::Separator();
        int l_GridSize = m_PushConstants.gridSize;
        ImGui::DragInt("Grid size", &l_GridSize, 1, 1, 100);
        m_PushConstants.gridSize = static_cast<uint32_t>(l_GridSize);
        ImGui::DragFloat("Patch size", &m_PushConstants.patchSize, 0.1f, 1.f, 100.f);
        ImGui::Separator();
        ImGui::DragFloat("Tessellation min", &m_PushConstants.minTessLevel, 0.1f, 1.f, 64.f);
        if (m_PushConstants.minTessLevel > m_PushConstants.maxTessLevel)
            m_PushConstants.minTessLevel = m_PushConstants.maxTessLevel;
        ImGui::DragFloat("Tessellation max", &m_PushConstants.maxTessLevel, 0.1f, 1.f, 64.f);
        if (m_PushConstants.minTessLevel > m_PushConstants.maxTessLevel)
            m_PushConstants.maxTessLevel = m_PushConstants.minTessLevel;
        ImGui::DragFloat("Tessellation factor", &m_PushConstants.tessFactor, 0.001f, 0.01f, 1.f);
        ImGui::DragFloat("Tessellation slope", &m_PushConstants.tessSlope, 0.01f, 0.01f, 2.f);
        ImGui::Separator();
        ImGui::ColorEdit3("Color", &m_PushConstants.color.x);
        ImGui::Separator();
        ImGui::Checkbox("Wireframe", &m_Wireframe);
        ImGui::Separator();
        ImGui::Checkbox("Noise Hot Reload", &m_HotReload);
        if (!m_HotReload)
        {
            ImGui::DragFloat2("Noise offset", &m_ComputePushConstants.offset.x, 0.01f);
            ImGui::DragFloat("Noise scale", &m_ComputePushConstants.scale, 0.01f);
            ImGui::DragFloat("Noise W", &m_ComputePushConstants.w, 0.01f);
            if (ImGui::Button("Recompute Noise"))
            {
                m_NoiseDirty = true;
                m_NormalDirty = true;
            }
        }
        else
        {
            glm::vec2 l_Offset = m_ComputePushConstants.offset;
            ImGui::DragFloat2("Noise offset", &l_Offset.x, 0.01f);
            if (l_Offset != m_ComputePushConstants.offset)
            {
                m_ComputePushConstants.offset = l_Offset;
                m_NoiseDirty = true;
                m_NormalDirty = true;
            }
            float l_Scale = m_ComputePushConstants.scale;
            ImGui::DragFloat("Noise scale", &l_Scale, 0.001f, 0.001f, 1.f);
            if (l_Scale != m_ComputePushConstants.scale)
            {
                m_ComputePushConstants.scale = l_Scale;
                m_NoiseDirty = true;
                m_NormalDirty = true;
            }
            float l_W = m_W;
            ImGui::DragFloat("Noise W", &l_W, 0.01f);
            if (l_W != m_W)
            {
                m_W = l_W;
                m_NoiseDirty = true;
                m_NormalDirty = true;
            }
            ImGui::Checkbox("Animated W", &m_WAnimated);
            if (m_WAnimated)
            {
                ImGui::DragFloat("W speed", &m_WSpeed, 0.01f);

                m_WOffset += m_WSpeed * ImGui::GetIO().DeltaTime;
                m_ComputePushConstants.w = m_W + m_WOffset;
                m_NoiseDirty = true;
                m_NormalDirty = true;
            }
        }
        ImGui::Separator();
        ImGui::BeginDisabled(m_ShowImagePanel);
        if (ImGui::Button("Preview Images"))
        {
            m_ShowImagePanel = true;
            m_ImagePreview = NONE;
        }
        ImGui::EndDisabled();
        ImGui::End();

        if (m_ShowImagePanel)
        {
            ImGui::Begin("Images", &m_ShowImagePanel);
            // ComboBox
            const char* l_ImageNames[] = { "None", "Heightmap", "Normalmap" };
            if (ImGui::BeginCombo("Image", l_ImageNames[m_ImagePreview]))
            {
                for (int i = 0; i < 3; i++)
                {
                    const bool l_Selected = (m_ImagePreview == i);
                    if (ImGui::Selectable(l_ImageNames[i], l_Selected))
                    {
                        m_ImagePreview = static_cast<ImagePreview>(i);
                    }
                    if (l_Selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::End();

            switch (m_ImagePreview)
            {
            case NONE:
                break;
            case HEIGHTMAP:
                if (m_HeightmapDescriptorSet == VK_NULL_HANDLE)
                    break;
                {
                    const VkExtent3D l_Size = l_Device.getImage(m_HeightmapID).getSize();

                    ImGui::Begin("Texture");
                    ImGui::Image(reinterpret_cast<ImTextureID>(m_HeightmapDescriptorSet), ImVec2(l_Size.width, l_Size.height));
                    ImGui::End();
                }
                break;
            case NORMALMAP:
                if (m_NormalmapDescriptorSet == VK_NULL_HANDLE)
                    break;
                {
                    const VkExtent3D l_Size = l_Device.getImage(m_HeightmapID).getSize(); // Should be normalmap

                    ImGui::Begin("Texture");
                    ImGui::Image(reinterpret_cast<ImTextureID>(m_NormalmapDescriptorSet), ImVec2(l_Size.width, l_Size.height));
                    ImGui::End();
                }
                break;
            }
        }

        
    }

    ImGui::Render();
}
