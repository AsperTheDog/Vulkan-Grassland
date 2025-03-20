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
    : m_Window("Vulkan", 1920, 1080), m_Camera(glm::vec3{0.f, -20.f, 0.f}, glm::vec3{0.f, 0.f, -1.f})
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
    l_Selector.selectQueueFamily(l_ComputeQueueFamily, QueueFamilyTypeBits::COMPUTE);
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
    m_RenderCmdBufferID = l_Device.createCommandBuffer(l_GraphicsQueueFamily, 0, false);
    l_Device.initializeCommandPool(l_ComputeQueueFamily, 0, true);
    m_HeightmapCmdBufferID = l_Device.createCommandBuffer(l_ComputeQueueFamily, 0, false);
    m_GrassHeightCmdBufferID = l_Device.createCommandBuffer(l_ComputeQueueFamily, 0, false);
    m_WindCmdBufferID = l_Device.createCommandBuffer(l_ComputeQueueFamily, 0, false);
    m_ComputeCmdBufferID = l_Device.createCommandBuffer(l_ComputeQueueFamily, 0, false);
    l_Device.initializeCommandPool(l_TransferQueueFamily, 0, true);
    m_TransferCmdBufferID = l_Device.createCommandBuffer(l_TransferQueueFamily, 0, false);

    // Depth Buffer
    m_DepthBuffer = l_Device.createImage(VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT, { l_Swapchain.getExtent().width, l_Swapchain.getExtent().height, 1 }, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0);
    VulkanImage& l_DepthImage = l_Device.getImage(m_DepthBuffer);
    l_DepthImage.allocateFromFlags({ .desiredProperties= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, .undesiredProperties= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, .allowUndesired= false});
    m_DepthBufferView = l_DepthImage.createImageView(VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

    l_Device.configureStagingBuffer(100LL * 1024 * 1024, m_TransferQueuePos);

    //Descriptor pool
    std::array<VkDescriptorPoolSize, 3> l_PoolSizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
    };
    m_DescriptorPoolID = l_Device.createDescriptorPool(l_PoolSizes, 7, 0);

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
    m_HeightmapFinishedSemaphoreID = l_Device.createSemaphore();
    m_GrassHeightFinishedSemaphoreID = l_Device.createSemaphore();
    m_WindFinishedSemaphoreID = l_Device.createSemaphore();
    m_TransferFinishedSemaphoreID = l_Device.createSemaphore();
    m_ComputeFinishedSemaphoreID = l_Device.createSemaphore();
    m_RenderFinishedSemaphoreID = l_Device.createSemaphore();

    m_RenderFenceID = l_Device.createFence(true);
    m_ComputeFenceID = l_Device.createFence(false);

    m_Window.getMouseMovedSignal().connect(&m_Camera, &Camera::mouseMoved);
    m_Window.getKeyPressedSignal().connect(&m_Camera, &Camera::keyPressed);
    m_Window.getKeyReleasedSignal().connect(&m_Camera, &Camera::keyReleased);
    m_Window.getEventsProcessedSignal().connect(&m_Camera, &Camera::updateEvents);
    m_Window.getMouseCaptureChangedSignal().connect(&m_Camera, &Camera::setMouseCaptured);
    m_Window.getResizedSignal().connect(this, &Engine::recreateSwapchain);
    m_Window.getMouseScrolledSignal().connect(&m_Camera, &Camera::mouseScrolled);

    m_NoiseEngine.initialize();
    m_Heightmap.initialize(1024, *this, true);

    m_PlaneEngine.initialize();
    m_GrassEngine.initalize({7, 11, 17, 31}, {120, 100, 80, 60});

    initImgui();
    m_NoiseEngine.initializeImgui();
    m_Heightmap.initializeImgui();

    m_PlaneEngine.initializeImgui();
    m_GrassEngine.initializeImgui();

    m_Window.toggleMouseCapture();

    setLightDir(0.f, 0.5f);
}

Engine::~Engine()
{
    VulkanContext::getDevice(m_DeviceID).waitIdle();

    Logger::setRootContext("Resource cleanup");

    m_PlaneEngine.cleanupImgui();
    m_Heightmap.cleanupImgui();
    m_GrassEngine.cleanupImgui();
    m_NoiseEngine.cleanupImgui();

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

    VulkanFence& l_RenderFence = l_Device.getFence(m_RenderFenceID);
    VulkanFence& l_ComputeFence = l_Device.getFence(m_ComputeFenceID);

    m_CurrentFrame = 0;
    while (!m_Window.shouldClose())
    {
        m_Window.pollEvents();
        if (m_Window.isMinimized())
            continue;

        update();
        
        if (m_MustWaitForGrass)
        {
            l_ComputeFence.wait();
            l_ComputeFence.reset();
            m_MustWaitForGrass = false;
        }

        const bool l_RenderedGrassHeight = computeGrassHeight();
        const bool l_TransferredCullData = transferCulling();

        l_RenderFence.wait();
        l_RenderFence.reset();

        const bool l_RenderedHeightmap = computeHeightmap();
        const bool l_RenderedWind = computeWind();

        // Record
        bool l_ComputedGrass;
        {
            l_ComputedGrass = updateGrass(l_RenderedGrassHeight, l_TransferredCullData, l_RenderedHeightmap);
        }

        VulkanSwapchain& l_Swapchain = l_SwapchainExt->getSwapchain(m_SwapchainID);
        const uint32_t l_ImageIndex = l_Swapchain.acquireNextImage();
        if (l_ImageIndex == UINT32_MAX)
            continue;

        ImDrawData* l_ImguiDrawData = ImGui::GetDrawData();

        if (l_ImguiDrawData->DisplaySize.x <= 0.0f || l_ImguiDrawData->DisplaySize.y <= 0.0f)
            continue;

        // Render
        render(l_ImageIndex, l_ImguiDrawData, l_Swapchain.getImgSemaphore(), l_RenderedHeightmap, l_ComputedGrass, l_RenderedWind);

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

void Engine::setLightDir(const float p_Azimuth, const float p_Altitude)
{
    m_LightDirAltitude = p_Altitude;
    m_LightDirAzimuth = p_Azimuth;
    const float l_Altitude = m_LightDirAltitude * glm::half_pi<float>() + glm::half_pi<float>();
    const float l_Azimuth = m_LightDirAzimuth * glm::two_pi<float>();
    m_LightDir = glm::normalize(glm::vec3{
        std::cos(l_Altitude) * std::sin(l_Azimuth),
        -std::sin(l_Altitude),
        std::cos(l_Altitude) * std::cos(l_Azimuth)
    });
}

glm::vec3 Engine::getLightDir() const
{
    return m_LightDir;
}

void Engine::update()
{
    Engine::drawImgui();

    const glm::vec2 l_CameraTile = m_Camera.getTiledPosition(m_PlaneEngine.getTileSize());
    if (l_CameraTile != m_PlaneEngine.getCameraTile())
    {
        const glm::vec2 l_NoiseOffset = l_CameraTile / m_PlaneEngine.getGridExtent();
        m_GrassEngine.changeCurrentCenter(l_CameraTile, l_NoiseOffset);
        m_Heightmap.updateOffset(l_NoiseOffset);
    }

    m_PlaneEngine.update(l_CameraTile);

    m_Heightmap.updatePatchSize(m_PlaneEngine.getTileSize());
    m_Heightmap.updateGridSize(m_PlaneEngine.getGridSize());
    m_Heightmap.updateHeightScale(m_PlaneEngine.getHeightScale());

    m_GrassEngine.update(l_CameraTile, m_PlaneEngine.getHeightScale(), m_PlaneEngine.getTileSize());

    if (m_Heightmap.isDirty())
        m_GrassEngine.setDirty();
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

void Engine::render(const uint32_t l_ImageIndex, ImDrawData* p_ImGuiDrawData, const ResourceID p_SwapchainSemaphore, const bool p_RenderedHeightmap, const bool p_ComputedGrass, const bool p_RenderedWind)
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);

    VulkanCommandBuffer& p_CmdBuffer = VulkanContext::getDevice(m_DeviceID).getCommandBuffer(m_RenderCmdBufferID, 0);
    
    const VkExtent2D& extent = getSwapchain().getExtent();

    std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    clearValues[1].depthStencil = { .depth= 1.0f, .stencil= 0};

    p_CmdBuffer.beginRecording();
    p_CmdBuffer.cmdBeginRenderPass(m_RenderPassID, m_FramebufferIDs[l_ImageIndex], extent, clearValues);

    m_PlaneEngine.render(p_CmdBuffer);
    m_GrassEngine.render(p_CmdBuffer);

    ImGui_ImplVulkan_RenderDrawData(p_ImGuiDrawData, *p_CmdBuffer);

    p_CmdBuffer.cmdEndRenderPass();
    p_CmdBuffer.endRecording();

    std::vector<VulkanCommandBuffer::WaitSemaphoreData> l_WaitSemaphores;
    l_WaitSemaphores.emplace_back(p_SwapchainSemaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (p_ComputedGrass)
        l_WaitSemaphores.emplace_back(m_ComputeFinishedSemaphoreID, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    else if (p_RenderedHeightmap)
        l_WaitSemaphores.emplace_back(m_HeightmapFinishedSemaphoreID, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (p_RenderedWind)
        l_WaitSemaphores.emplace_back(m_WindFinishedSemaphoreID, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    const VulkanQueue l_GraphicsQueue = l_Device.getQueue(m_GraphicsQueuePos);
    VulkanCommandBuffer& l_GraphicsBuffer = l_Device.getCommandBuffer(m_RenderCmdBufferID, 0);

    const std::array<ResourceID, 1> l_SignalSemaphores = { m_RenderFinishedSemaphoreID };
    l_GraphicsBuffer.submit(l_GraphicsQueue, l_WaitSemaphores, l_SignalSemaphores, m_RenderFenceID);
}

bool Engine::computeHeightmap()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    VulkanCommandBuffer& l_Buffer = l_Device.getCommandBuffer(m_HeightmapCmdBufferID, 0);

    const bool l_Recomputed = m_NoiseEngine.recalculate(l_Buffer, m_Heightmap);

    if (l_Recomputed)
    {
        l_Buffer.endRecording();

        const VulkanQueue l_ComputeQueue = l_Device.getQueue(m_ComputeQueuePos);
        const std::array<ResourceID, 1> l_SignalSemaphores = { m_HeightmapFinishedSemaphoreID };
        l_Buffer.submit(l_ComputeQueue, {}, l_SignalSemaphores);
    }

    return l_Recomputed;
}

bool Engine::computeGrassHeight()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    VulkanCommandBuffer& l_Buffer = l_Device.getCommandBuffer(m_GrassHeightCmdBufferID, 0);

    const bool l_Recomputed = m_GrassEngine.recomputeHeight(l_Buffer);

    if (l_Recomputed)
    {
        l_Buffer.endRecording();

        const VulkanQueue l_ComputeQueue = l_Device.getQueue(m_ComputeQueuePos);
        const std::array<ResourceID, 1> l_SignalSemaphores = { m_GrassHeightFinishedSemaphoreID };
        l_Buffer.submit(l_ComputeQueue, {}, l_SignalSemaphores);
    }

    return l_Recomputed;
}

bool Engine::computeWind()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    VulkanCommandBuffer& l_Buffer = l_Device.getCommandBuffer(m_WindCmdBufferID, 0);

    const bool l_Recomputed = m_GrassEngine.recomputeWind(l_Buffer);

    if (l_Recomputed)
    {
        l_Buffer.endRecording();

        const VulkanQueue l_ComputeQueue = l_Device.getQueue(m_ComputeQueuePos);
        const std::array<ResourceID, 1> l_SignalSemaphores = { m_WindFinishedSemaphoreID };
        l_Buffer.submit(l_ComputeQueue, {}, l_SignalSemaphores);
    }

    return l_Recomputed;
}

bool Engine::updateGrass(const bool p_GrassHeightComputed, const bool p_DataTransferred, const bool p_HeightmapComputed)
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    VulkanCommandBuffer& l_Buffer = l_Device.getCommandBuffer(m_ComputeCmdBufferID, 0);

    const bool l_Recomputed = m_GrassEngine.recompute(l_Buffer, m_PlaneEngine.getTileSize(), m_PlaneEngine.getGridSize(), m_PlaneEngine.getHeightScale());

    if (l_Recomputed)
    {
        l_Buffer.endRecording();

        const VulkanQueue l_ComputeQueue = l_Device.getQueue(m_ComputeQueuePos);
        const std::array<ResourceID, 1> l_SignalSemaphores = { m_ComputeFinishedSemaphoreID };
        std::vector<VulkanCommandBuffer::WaitSemaphoreData> l_WaitSemaphores;
        if (p_GrassHeightComputed)
            l_WaitSemaphores.emplace_back(m_GrassHeightFinishedSemaphoreID, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (p_DataTransferred)
            l_WaitSemaphores.emplace_back(m_TransferFinishedSemaphoreID, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (p_HeightmapComputed)
            l_WaitSemaphores.emplace_back(m_HeightmapFinishedSemaphoreID, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        l_Buffer.submit(l_ComputeQueue, l_WaitSemaphores, l_SignalSemaphores, m_ComputeFenceID);
        m_MustWaitForGrass = true;
    }

    return l_Recomputed;
}

bool Engine::transferCulling()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);
    VulkanCommandBuffer& l_Buffer = l_Device.getCommandBuffer(m_TransferCmdBufferID, 0);

    const bool l_Transferred = m_GrassEngine.transferCulling(l_Buffer);

    if (l_Transferred)
    {
        l_Buffer.endRecording();

        const VulkanQueue l_TransferQueue = l_Device.getQueue(m_TransferQueuePos);
        const std::array<ResourceID, 1> l_SignalSemaphores = { m_TransferFinishedSemaphoreID };
        l_Buffer.submit(l_TransferQueue, {}, l_SignalSemaphores);
    }

    return l_Transferred;
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
        { .type= VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount= 1000},
	    { .type= VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount= 1000},
	    { .type= VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount= 1000},
	    { .type= VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount= 1000},
	    { .type= VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, .descriptorCount= 1000},
	    { .type= VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, .descriptorCount= 1000},
	    { .type= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount= 1000},
	    { .type= VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount= 1000},
	    { .type= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .descriptorCount= 1000},
	    { .type= VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, .descriptorCount= 1000},
	    { .type= VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, .descriptorCount= 1000}
	    }
    };
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

    ImGui::Begin("General");
    
    float l_LightDirAzimuth = m_LightDirAzimuth;
    ImGui::DragFloat("Light azimuth", &l_LightDirAzimuth, 0.01f, 0.f, 1.f);
    float l_LightDirAltitude = m_LightDirAltitude;
    ImGui::DragFloat("Light altitude", &l_LightDirAltitude, 0.01f, 0.f, 1.f);

    if (l_LightDirAzimuth != m_LightDirAzimuth || l_LightDirAltitude != m_LightDirAltitude)
    {
        setLightDir(l_LightDirAzimuth, l_LightDirAltitude);
    }

    ImGui::Separator();

    if (ImGui::Button("Edit heightmap"))
        m_Heightmap.toggleImgui();

    ImGui::End();

    m_Heightmap.drawImgui("Heightmap");

    m_PlaneEngine.drawImgui();
    m_GrassEngine.drawImgui();
    m_NoiseEngine.drawImgui();

    ImGui::Render();
}
