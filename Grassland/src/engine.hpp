#pragma once
#include <utils/identifiable.hpp>

#include "sdl_window.hpp"
#include "vulkan_queues.hpp"


class Engine
{
public:
    Engine();
    ~Engine();
    void run();

private:
    void createRenderPasses();
    void createPipelines();

    void recreateSwapchain(VkExtent2D p_NewSize);

    SDLWindow m_Window;

    QueueSelection m_GraphicsQueuePos;
    QueueSelection m_PresentQueuePos;
    QueueSelection m_TransferQueuePos;

    ResourceID m_DeviceID;

    ResourceID m_SwapchainID;

    ResourceID m_GraphicsCmdBufferID;

    ResourceID m_DepthBuffer;
    ResourceID m_DepthBufferView;
	std::vector<ResourceID> m_FramebufferIDs{};

    ResourceID m_RenderPassID;

    ResourceID m_VertexBufferID;
    ResourceID m_IndexBufferID;
    ResourceID m_GraphicsPipelineID;

    ResourceID m_RenderFinishedSemaphoreID;
    ResourceID m_InFlightFenceID;

private:
    void initImgui() const;
    void drawImgui() const;
};
