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

    VulkanContext::initializeTransientMemory(1LL * 1024 * 1024);
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
    m_PresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    VulkanSwapchainExtension* l_SwapchainExt = VulkanSwapchainExtension::get(m_DeviceID);
    m_SwapchainID = l_SwapchainExt->createSwapchain(m_Window.getSurface(), m_Window.getSize().toExtent2D(), { VK_FORMAT_R8G8B8A8_SRGB, VK_COLORSPACE_SRGB_NONLINEAR_KHR }, m_PresentMode);
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
    std::array<VkDescriptorPoolSize, 3> l_PoolSizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
    };
    m_DescriptorPoolID = l_Device.createDescriptorPool(l_PoolSizes, 4, 0);

    // Renderpass and pipelines
    createRenderPasses();

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
    if (!m_UsingSharedCmdBuffer)
        m_ComputeFinishedSemaphoreID = l_Device.createSemaphore();

    m_Window.getMouseMovedSignal().connect(&m_Camera, &Camera::mouseMoved);
    m_Window.getKeyPressedSignal().connect(&m_Camera, &Camera::keyPressed);
    m_Window.getKeyReleasedSignal().connect(&m_Camera, &Camera::keyReleased);
    m_Window.getEventsProcessedSignal().connect(&m_Camera, &Camera::updateEvents);
    m_Window.getMouseCaptureChangedSignal().connect(&m_Camera, &Camera::setMouseCaptured);
    m_Window.getResizedSignal().connect(this, &Engine::recreateSwapchain);
    m_Window.getMouseScrolledSignal().connect(&m_Camera, &Camera::mouseScrolled);

    m_Plane.initialize(512);
    m_Grass.initalize({ m_Plane.getHeightmapID(), m_Plane.getHeightmapViewID(), m_Plane.getHeightmapSamplerID() }, {13, 19, 30}, {130, 100, 80});

    initImgui();
    m_Plane.initializeImgui();
    m_Grass.initializeImgui();

    m_Window.toggleMouseCapture();
}

Engine::~Engine()
{
    VulkanContext::getDevice(m_DeviceID).waitIdle();

    Logger::setRootContext("Resource cleanup");

    m_Plane.cleanup();

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

    m_CurrentFrame = 0;
    while (!m_Window.shouldClose())
    {
        m_Window.pollEvents();
        if (m_Window.isMinimized())
        {
            continue;
        }

        Engine::drawImgui();
        m_Plane.update();
        m_Grass.update(m_Plane.getCameraTile());

        l_InFlightFence.wait();
        l_InFlightFence.reset();

        VulkanSwapchain& l_Swapchain = l_SwapchainExt->getSwapchain(m_SwapchainID);
        const uint32_t l_ImageIndex = l_Swapchain.acquireNextImage();
        if (l_ImageIndex == UINT32_MAX)
        {
            continue;
        }

        ImDrawData* l_ImguiDrawData = ImGui::GetDrawData();

        if (l_ImguiDrawData->DisplaySize.x <= 0.0f || l_ImguiDrawData->DisplaySize.y <= 0.0f)
        {
            continue;
        }

        // Record
        bool l_RenderedNoise;
        {
            l_RenderedNoise = renderNoise();
            l_RenderedNoise = updateGrass(l_RenderedNoise);
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
                if (l_RenderedNoise)
                {
                    const std::array<VulkanCommandBuffer::WaitSemaphoreData, 2> l_WaitSemaphores = {
                        VulkanCommandBuffer::WaitSemaphoreData{l_Swapchain.getImgSemaphore(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
                        VulkanCommandBuffer::WaitSemaphoreData{m_ComputeFinishedSemaphoreID, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT}
                    };
                    l_GraphicsBuffer.submit(l_GraphicsQueue, l_WaitSemaphores, l_SignalSemaphores, m_InFlightFenceID);
                }
                else
                {
                    const std::array<VulkanCommandBuffer::WaitSemaphoreData, 1> l_WaitSemaphores = {
                        VulkanCommandBuffer::WaitSemaphoreData{l_Swapchain.getImgSemaphore(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT}
                    };
                    l_GraphicsBuffer.submit(l_GraphicsQueue, l_WaitSemaphores, l_SignalSemaphores, m_InFlightFenceID);
                }
            }
        }

        // Present
        {
            std::array<ResourceID, 1> l_Semaphores = { m_RenderFinishedSemaphoreID };
            l_Swapchain.present(m_PresentQueuePos, l_Semaphores);
        }

        VulkanContext::resetTransMemory();
        m_CurrentFrame++;
    }
}

VulkanDevice& Engine::getDevice() const
{
    return VulkanContext::getDevice(m_DeviceID);
}

VulkanSwapchain& Engine::getSwapchain() const
{
    return VulkanSwapchainExtension::get(m_DeviceID)->getSwapchain(m_SwapchainID);
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

void Engine::render(const uint32_t l_ImageIndex, ImDrawData* p_ImGuiDrawData, const bool p_UsedCompute) const
{
    VulkanCommandBuffer& p_Buffer = VulkanContext::getDevice(m_DeviceID).getCommandBuffer(m_GraphicsCmdBufferID, 0);
    
    const VkExtent2D& extent = getSwapchain().getExtent();

    std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    if (!m_UsingSharedCmdBuffer || !p_UsedCompute)
        p_Buffer.beginRecording();
    p_Buffer.cmdBeginRenderPass(m_RenderPassID, m_FramebufferIDs[l_ImageIndex], extent, clearValues);

    m_Plane.render(p_Buffer);
    m_Grass.render(p_Buffer);

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

    if (m_NoiseDirty)
    {
        m_Plane.updateNoise(l_Buffer);
        m_NoiseDirty = false;
    }

    if (m_NormalDirty)
    {
        m_Plane.updateNormal(l_Buffer);
        m_NormalDirty = false;
    }

    return l_NeedsCompute;
}

bool Engine::updateGrass(const bool p_ComputedPlane)
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    VulkanCommandBuffer& l_Buffer = l_Device.getCommandBuffer(m_ComputeCmdBufferID, 0);

    const bool l_NeedsCompute = m_GrassDirty || p_ComputedPlane;

    if (l_NeedsCompute && !p_ComputedPlane)
    {
        l_Buffer.reset();
        l_Buffer.beginRecording();
    }

    if (l_NeedsCompute)
    {
        m_Grass.recompute(l_Buffer, m_Plane.getTileSize(), m_Plane.getGridExtent(), m_Plane.getHeightmapScale(), m_GraphicsQueuePos.familyIndex);
        m_GrassDirty = false;
    }

    if (l_NeedsCompute && !m_UsingSharedCmdBuffer)
    {
        l_Buffer.endRecording();

        const VulkanQueue l_ComputeQueue = l_Device.getQueue(m_ComputeQueuePos);
        const std::array<ResourceID, 1> l_SignalSemaphores = { m_ComputeFinishedSemaphoreID };
        l_Buffer.submit(l_ComputeQueue, {}, l_SignalSemaphores);
    }

    return l_NeedsCompute;
}

void Engine::recreateSwapchain(const VkExtent2D p_NewSize)
{
    Logger::pushContext("Recreate Swapchain");
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    l_Device.waitIdle();

    VulkanSwapchainExtension* swapchainExtension = VulkanSwapchainExtension::get(l_Device);

    m_SwapchainID = swapchainExtension->createSwapchain(m_Window.getSurface(), p_NewSize, swapchainExtension->getSwapchain(m_SwapchainID).getFormat(), m_PresentMode, m_SwapchainID);

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
    ImGui_ImplVulkan_Init(&l_InitInfo);
}

void Engine::drawImgui()
{
    ImGui_ImplVulkan_NewFrame();
    m_Window.frameImgui();
    ImGui::NewFrame();

    ImGui::Begin("Info");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Camera position (%.2f, %.2f, %.2f)", m_Camera.getPosition().x, m_Camera.getPosition().y, m_Camera.getPosition().z);
    ImGui::End();

    m_Plane.drawImgui();
    m_Grass.drawImgui();

    ImGui::Render();
}
