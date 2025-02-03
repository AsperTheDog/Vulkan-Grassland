#include "engine.hpp"

#include <array>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include "vertex.hpp"

#include "vulkan_context.hpp"
#include "ext/vulkan_swapchain.hpp"
#include "utils/logger.hpp"

constexpr std::array<Vertex, 3> vertices = {
    Vertex{ { 0.0f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
    Vertex{ { 0.5f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
    Vertex{ { -0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
};

constexpr std::array<uint16_t, 3> indices = { 0, 1, 2 };

static VulkanGPU chooseCorrectGPU()
{
    const std::vector<VulkanGPU> l_GPUs = VulkanContext::getGPUs();
    for (auto& l_GPU : l_GPUs)
    {
        if (l_GPU.getProperties().deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            return l_GPU;
        }
    }

    throw std::runtime_error("No discrete GPU found");
}

Engine::Engine() : m_Window("Vulkan", 1920, 1080)
{
    // Vulkan Instance
    Logger::setRootContext("Engine init");
#ifndef _DEBUG
    Logger::setLevels(Logger::WARN | Logger::ERR);
    VulkanContext::init(VK_API_VERSION_1_3, false, false, m_window.getRequiredVulkanExtensions());
#else
    Logger::setLevels(Logger::ALL);
    VulkanContext::init(VK_API_VERSION_1_3, true, false, m_Window.getRequiredVulkanExtensions());
#endif
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
    m_DeviceID = VulkanContext::createDevice(l_GPU, l_Selector, &l_Extensions, {});
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

    // Vertex Buffer
    l_Device.configureStagingBuffer(5LL * 1024 * 1024, m_TransferQueuePos);
    m_VertexBufferID = l_Device.createBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VulkanBuffer& l_VertexBuffer = l_Device.getBuffer(m_VertexBufferID);
    l_VertexBuffer.allocateFromFlags({ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false });
    l_Device.dumpDataIntoBuffer(m_VertexBufferID, reinterpret_cast<const uint8_t*>(vertices.data()), sizeof(vertices), 0);

    // Index Buffer
    m_IndexBufferID = l_Device.createBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VulkanBuffer& l_IndexBuffer = l_Device.getBuffer(m_IndexBufferID);
    l_IndexBuffer.allocateFromFlags({ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false });
    l_Device.dumpDataIntoBuffer(m_IndexBufferID, reinterpret_cast<const uint8_t*>(indices.data()), sizeof(indices), 0);

    // Renderpass and pipelines
    createRenderPasses();
    createPipelines();

    // Framebuffers
    m_FramebufferIDs.resize(l_Swapchain.getImageCount());
    for (uint32_t i = 0; i < l_Swapchain.getImageCount(); i++)
    {
        VkImageView l_Color = *l_Swapchain.getImage(i).getImageView(l_Swapchain.getImageView(i));
        const std::vector<VkImageView> l_Attachments = { l_Color, *l_Device.getImage(m_DepthBuffer).getImageView(m_DepthBufferView) };
        m_FramebufferIDs[i] = VulkanContext::getDevice(m_DeviceID).createFramebuffer({ l_Swapchain.getExtent().width, l_Swapchain.getExtent().height, 1 }, m_RenderPassID, l_Attachments);
    }

    // Sync objects
    m_RenderFinishedSemaphoreID = l_Device.createSemaphore();
    m_InFlightFenceID = l_Device.createFence(true);

    
    m_Window.getResizedSignal().connect(this, &Engine::recreateSwapchain);

    initImgui();
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

        // Recording
        {
            const VkExtent2D& extent = l_SwapchainExt->getSwapchain(m_SwapchainID).getExtent();

            std::vector<VkClearValue> clearValues{ 2 };
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

            l_GraphicsBuffer.reset();
            l_GraphicsBuffer.beginRecording();

            l_GraphicsBuffer.cmdBeginRenderPass(m_RenderPassID, m_FramebufferIDs[l_ImageIndex], extent, clearValues);
            l_GraphicsBuffer.cmdBindVertexBuffer(m_VertexBufferID, 0);
            l_GraphicsBuffer.cmdBindIndexBuffer(m_IndexBufferID, 0, VK_INDEX_TYPE_UINT16);
            l_GraphicsBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipelineID);
            l_GraphicsBuffer.cmdSetViewport(viewport);
            l_GraphicsBuffer.cmdSetScissor(scissor);
            l_GraphicsBuffer.cmdDrawIndexed(3, 0, 0);

            ImGui_ImplVulkan_RenderDrawData(l_ImguiDrawData, *l_GraphicsBuffer);

            l_GraphicsBuffer.cmdEndRenderPass();
            l_GraphicsBuffer.endRecording();
        }

        l_GraphicsBuffer.submit(l_GraphicsQueue, { {l_Swapchain.getImgSemaphore(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT} }, { m_RenderFinishedSemaphoreID }, m_InFlightFenceID);

        l_Swapchain.present(m_PresentQueuePos, {m_RenderFinishedSemaphoreID});
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

    l_Builder.addSubpass(VK_PIPELINE_BIND_POINT_GRAPHICS, { {COLOR, 0}, {DEPTH_STENCIL, 1} }, 0);

    m_RenderPassID = VulkanContext::getDevice(m_DeviceID).createRenderPass(l_Builder, 0);
    Logger::popContext();
}

void Engine::createPipelines()
{
    VulkanDevice& l_Device = VulkanContext::getDevice(m_DeviceID);

	const uint32_t l_Layout = l_Device.createPipelineLayout({}, {});

	const uint32_t l_VertexShader = l_Device.createShader("shaders/shader.vert", VK_SHADER_STAGE_VERTEX_BIT, false, {});
	const uint32_t l_FragmentShader = l_Device.createShader("shaders/shader.frag", VK_SHADER_STAGE_FRAGMENT_BIT, false, {});

	VkPipelineColorBlendAttachmentState l_ColorBlendAttachment{};
	l_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	l_ColorBlendAttachment.blendEnable = VK_FALSE;

    VulkanBinding l_Binding{0, VK_VERTEX_INPUT_RATE_VERTEX, sizeof(Vertex)};
	l_Binding.addAttribDescription(VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position));
	l_Binding.addAttribDescription(VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));

	VulkanPipelineBuilder l_Builder{ m_DeviceID };
    l_Builder.addVertexBinding(l_Binding);
	l_Builder.setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
	l_Builder.setViewportState(1, 1);
	l_Builder.setRasterizationState(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
	l_Builder.setMultisampleState(VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 1.0f);
	l_Builder.setDepthStencilState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);
	l_Builder.addColorBlendAttachment(l_ColorBlendAttachment);
	l_Builder.setColorBlendState(VK_FALSE, VK_LOGIC_OP_COPY, {0.0f, 0.0f, 0.0f, 0.0f});
	l_Builder.setDynamicState({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR });
    l_Builder.addShaderStage(l_VertexShader);
    l_Builder.addShaderStage(l_FragmentShader);
	m_GraphicsPipelineID = l_Device.createPipeline(l_Builder, l_Layout, m_RenderPassID, 0);
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
        const std::vector<VkImageView> l_Attachments = { l_Color, *l_Device.getImage(m_DepthBuffer).getImageView(m_DepthBufferView) };
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

    const std::vector<VkDescriptorPoolSize> l_PoolSizes =
    {
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
    };
    const uint32_t l_ImguiPoolID = l_Device.createDescriptorPool(l_PoolSizes, 1000U * static_cast<uint32_t>(l_PoolSizes.size()), VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

    VulkanSwapchainExtension* l_SwapchainExt = VulkanSwapchainExtension::get(l_Device);
    const VulkanSwapchain& l_Swapchain = l_SwapchainExt->getSwapchain(m_SwapchainID);

    m_Window.initImgui();
    ImGui_ImplVulkan_InitInfo l_InitInfo = {};
    l_InitInfo .Instance = VulkanContext::getHandle();
    l_InitInfo .PhysicalDevice = *l_Device.getGPU();
    l_InitInfo .Device = *l_Device;
    l_InitInfo .QueueFamily = m_GraphicsQueuePos.familyIndex;
    l_InitInfo .Queue = *l_Device.getQueue(m_GraphicsQueuePos);
    l_InitInfo .DescriptorPool = *l_Device.getDescriptorPool(l_ImguiPoolID);
    l_InitInfo .RenderPass = *l_Device.getRenderPass(m_RenderPassID);
    l_InitInfo .Subpass = 0;
    l_InitInfo .MinImageCount = l_Swapchain.getMinImageCount();
    l_InitInfo .ImageCount = l_Swapchain.getImageCount();
    l_InitInfo .MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&l_InitInfo );
}

void Engine::drawImgui() const
{
    ImGui_ImplVulkan_NewFrame();
    m_Window.frameImgui();
    ImGui::NewFrame();

    // ImGui here
    {
        ImGui::ShowDemoWindow();
    }

    ImGui::Render();
}
