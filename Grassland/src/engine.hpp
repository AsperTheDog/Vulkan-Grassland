#pragma once
#include <utils/identifiable.hpp>

#include "camera.hpp"
#include "grass_engine.hpp"
#include "imgui.h"
#include "plane_engine.hpp"
#include "pp_fog_engine.hpp"
#include "sdl_window.hpp"
#include "skybox_engine.hpp"
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
    [[nodiscard]] QueueSelection getPresentQueuePos() const { return m_PresentQueuePos; }
    [[nodiscard]] QueueSelection getTransferQueuePos() const { return m_TransferQueuePos; }
    Camera& getCamera() { return m_Camera; }
    NoiseEngine::NoiseObject& getHeightmap() { return m_Heightmap; }

    ResourceID getRenderImage() const { return m_RenderImageID; }
    ResourceID getRenderImageView() const { return m_RenderImageViewID; }
    ResourceID getDepthBuffer() const { return m_DepthBufferID; }
    ResourceID getDepthBufferView() const { return m_DepthBufferViewID; }


    void setLightDir(float p_Azimuth, float p_Altitude);
    [[nodiscard]] glm::vec3 getLightDir() const;
    [[nodiscard]] float getDelta() const;

private:
    void update();

    void createRenderPasses();

    void render(uint32_t l_ImageIndex, ImDrawData* p_ImGuiDrawData, ResourceID p_SwapchainSemaphore, bool p_RenderedHeightmap, bool p_ComputedGrass, bool p_RenderedWind);
    bool computeHeightmap();
    bool computeGrassHeight();
    bool computeWind();
    bool updateGrass(bool p_GrassHeightComputed, bool p_DataTransferred, bool p_HeightmapComputed);
    bool transferCulling();

    void recreateSwapchain(VkExtent2D p_NewSize);

    SDLWindow m_Window;

    Camera m_Camera;

    QueueSelection m_GraphicsQueuePos;
    QueueSelection m_ComputeQueuePos;
    QueueSelection m_PresentQueuePos;
    QueueSelection m_TransferQueuePos;

    ResourceID m_DeviceID = UINT32_MAX;

    ResourceID m_SwapchainID = UINT32_MAX;

    ResourceID m_GrassHeightCmdBufferID = UINT32_MAX;
    ResourceID m_HeightmapCmdBufferID = UINT32_MAX;
    ResourceID m_WindCmdBufferID = UINT32_MAX;
    ResourceID m_TransferCmdBufferID = UINT32_MAX;
    ResourceID m_ComputeCmdBufferID = UINT32_MAX;
    ResourceID m_RenderCmdBufferID = UINT32_MAX;

    ResourceID m_DepthBufferID = UINT32_MAX;
    ResourceID m_DepthBufferViewID = UINT32_MAX;

    ResourceID m_RenderImageID = UINT32_MAX;
    ResourceID m_RenderImageViewID = UINT32_MAX;

	std::vector<ResourceID> m_FramebufferIDs{};

    ResourceID m_RenderPassID = UINT32_MAX;
    
    ResourceID m_GrassHeightFinishedSemaphoreID = UINT32_MAX;
    ResourceID m_HeightmapFinishedSemaphoreID = UINT32_MAX;
    ResourceID m_WindFinishedSemaphoreID = UINT32_MAX;
    ResourceID m_TransferFinishedSemaphoreID = UINT32_MAX;
    ResourceID m_ComputeFinishedSemaphoreID = UINT32_MAX;
    ResourceID m_RenderFinishedSemaphoreID = UINT32_MAX;

    ResourceID m_ComputeFenceID = UINT32_MAX;
    ResourceID m_RenderFenceID = UINT32_MAX;

    ResourceID m_DescriptorPoolID = UINT32_MAX;

    uint32_t m_CurrentFrame = 0;

    VkPresentModeKHR m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;

private: // Plane
    PlaneEngine m_PlaneEngine{ *this };
    GrassEngine m_GrassEngine{ *this };
    NoiseEngine m_NoiseEngine{ *this };
    SkyboxEngine m_SkyboxEngine{ *this };
    PPFogEngine m_PPFogEngine{ *this };
    
    NoiseEngine::NoiseObject m_Heightmap{};

    glm::vec2 m_CurrentTile{ 0, 0 };
    glm::vec3 m_LightDir{};

    bool m_MustWaitForGrass = false;

    float m_Delta = 0.f;

private:
    void initImgui() const;
    void drawImgui();
    
    void toggleImgui();

    float m_LightDirAltitude = 0.f;
    float m_LightDirAzimuth = 0.f;

    bool m_ShowImGui = true;

private:
    friend class PlaneEngine;
};
