#pragma once
#include <utils/identifiable.hpp>

#include "camera.hpp"
#include "grass_engine.hpp"
#include "imgui.h"
#include "plane_engine.hpp"
#include "sdl_window.hpp"
#include "vulkan_queues.hpp"

class VulkanSwapchain;

class Engine
{
public:
    Engine();
    ~Engine();
    void run();

    [[nodiscard]] VulkanDevice& getDevice() const;
    [[nodiscard]] VulkanSwapchain& getSwapchain() const;
    [[nodiscard]] ResourceID getRenderPassID() const { return m_RenderPassID; }
    [[nodiscard]] ResourceID getDescriptorPoolID() const { return m_DescriptorPoolID; }

    [[nodiscard]] NoiseEngine& getNoiseEngine() { return m_NoiseEngine; }

    [[nodiscard]] bool isHeightmapDirty() const { return m_Heightmap.isNoiseDirty(); }
    [[nodiscard]] bool isGrassDirty() const { return m_GrassEngine.isDirty(); }

    [[nodiscard]] QueueSelection getGraphicsQueuePos() const { return m_GraphicsQueuePos; }
    [[nodiscard]] QueueSelection getComputeQueuePos() const { return m_ComputeQueuePos; }
    Camera& getCamera() { return m_Camera; }
    NoiseEngine::NoiseObject& getHeightmap() { return m_Heightmap; }

private:
    void update();

    void createRenderPasses();

    void render(uint32_t l_ImageIndex, ImDrawData* p_ImGuiDrawData, bool p_UsedCompute);
    bool renderNoise();
    bool updateGrass();

    void recreateSwapchain(VkExtent2D p_NewSize);

    SDLWindow m_Window;

    Camera m_Camera;

    QueueSelection m_GraphicsQueuePos;
    QueueSelection m_ComputeQueuePos;
    QueueSelection m_PresentQueuePos;
    QueueSelection m_TransferQueuePos;

    ResourceID m_DeviceID = UINT32_MAX;

    ResourceID m_SwapchainID = UINT32_MAX;

    ResourceID m_GraphicsCmdBufferID = UINT32_MAX;
    ResourceID m_ComputeCmdBufferID = UINT32_MAX;

    ResourceID m_ComputeFinishedSemaphoreID = UINT32_MAX;

    ResourceID m_DepthBuffer = UINT32_MAX;
    ResourceID m_DepthBufferView = UINT32_MAX;
	std::vector<ResourceID> m_FramebufferIDs{};

    ResourceID m_RenderPassID = UINT32_MAX;

    ResourceID m_RenderFinishedSemaphoreID = UINT32_MAX;
    ResourceID m_InFlightFenceID = UINT32_MAX;

    ResourceID m_DescriptorPoolID = UINT32_MAX;

    bool m_UsingSharedCmdBuffer = false;

    uint32_t m_CurrentFrame = 0;

    VkPresentModeKHR m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;

private: // Plane
    PlaneEngine m_PlaneEngine{ *this };
    GrassEngine m_GrassEngine{ *this };
    NoiseEngine m_NoiseEngine{ *this };
    
    NoiseEngine::NoiseObject m_Heightmap{};

    glm::vec2 m_CurrentTile{ 0, 0 };

private:
    void initImgui() const;
    void drawImgui();

private:
    friend class PlaneEngine;
};
