#pragma once
#include <utils/identifiable.hpp>

#include "camera.hpp"
#include "imgui.h"
#include "sdl_window.hpp"
#include "vulkan_queues.hpp"

struct PushConstantData
{
    alignas(4)  uint32_t gridSize = 200;
    alignas(4)  float patchSize = 3.f;
    alignas(4)  float minTessLevel = 1.f;
    alignas(4)  float maxTessLevel = 32.f;
    alignas(4)  float tessFactor = 0.014f;
    alignas(4)  float tessSlope = 0.07f;
    alignas(16) glm::vec3 cameraPos;
    alignas(4)  float heightScale = 10.f;
    alignas(16) glm::mat4 mvp;
    alignas(16) glm::vec3 color = { 0.0f, 1.0f, 0.0f };

    static uint32_t getVertexShaderOffset() { return 0; }
    static uint32_t getTessellationControlShaderOffset() { return offsetof(PushConstantData, minTessLevel); }
    static uint32_t getTessellationEvaluationShaderOffset() { return offsetof(PushConstantData, heightScale); }
    static uint32_t getFragmentShaderOffset() { return offsetof(PushConstantData, color); }

    static uint32_t getVertexShaderSize() { return offsetof(PushConstantData, minTessLevel); }
    static uint32_t getTessellationControlShaderSize() { return offsetof(PushConstantData, heightScale) - offsetof(PushConstantData, minTessLevel); }
    static uint32_t getTessellationEvaluationShaderSize() { return offsetof(PushConstantData, color) - offsetof(PushConstantData, heightScale); }
    static uint32_t getFragmentShaderSize() { return sizeof(PushConstantData) - offsetof(PushConstantData, color); }
};

struct NoisePushConstantData
{
    alignas(16) glm::vec2 offset;
    alignas(4) float w;
    alignas(4) float scale = 0.012f;
};

struct NormalPushConstantData
{
    alignas(4) float heightScale = 10.f;
    alignas(4) float offsetScale = 0.01f;
    alignas(4) float patchSize = 1.f;
    alignas(4) uint32_t gridSize = 100;
};

class Engine
{
public:
    Engine();
    ~Engine();
    void run();

private:
    void createRenderPasses();
    void createPipelines();
    void createHeightmapDescriptor(uint32_t p_TextWidth, uint32_t p_TextHeight);

    void render(uint32_t l_ImageIndex, ImDrawData* p_ImGuiDrawData, bool p_ComputedNoise);
    bool renderNoise();

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

    ResourceID m_GraphicsGrassPipelineID = UINT32_MAX;

    ResourceID m_RenderFinishedSemaphoreID = UINT32_MAX;
    ResourceID m_InFlightFenceID = UINT32_MAX;

    ResourceID m_HeightmapID = UINT32_MAX;
    ResourceID m_HeightmapViewID = UINT32_MAX;
    ResourceID m_HeightmapSamplerID = UINT32_MAX;

    ResourceID m_NormalmapID = UINT32_MAX;
    ResourceID m_NormalmapViewID = UINT32_MAX;
    ResourceID m_NormalmapSamplerID = UINT32_MAX;

    ResourceID m_DescriptorPoolID = UINT32_MAX;

    ResourceID m_TessellationPipelineID = UINT32_MAX;
    ResourceID m_TessellationPipelineWFID = UINT32_MAX;
    ResourceID m_TessellationPipelineLayoutID = UINT32_MAX;
    ResourceID m_TessellationDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_TessellationDescriptorSetID = UINT32_MAX;

    ResourceID m_ComputeNoisePipelineID = UINT32_MAX;
    ResourceID m_ComputeNormalPipelineID = UINT32_MAX;
    ResourceID m_ComputeNoisePipelineLayoutID = UINT32_MAX;
    ResourceID m_ComputeNormalPipelineLayoutID = UINT32_MAX;
    ResourceID m_ComputeNoiseDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_ComputeNoiseDescriptorSetID = UINT32_MAX;
    ResourceID m_ComputeNormalDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_ComputeNormalDescriptorSetID = UINT32_MAX;

    bool m_UsingSharedCmdBuffer = false;

    NoisePushConstantData m_NoisePushConstants;
    NormalPushConstantData m_NormalPushConstants;
    bool m_NoiseHotReload = true;
    bool m_NormalHotReload = true;
    bool m_WAnimated = false;
    float m_WSpeed = 0.1f;
    float m_WOffset = 0.0f;
    float m_W = 0.0f;
    
    PushConstantData m_PushConstants;

    bool m_Wireframe = false;

    bool m_NoiseDirty = true;
    bool m_NormalDirty = true;

private:
    void initImgui();
    void drawImgui();

    enum ImagePreview : uint8_t
    {
        NONE,
        HEIGHTMAP,
        NORMALMAP
    };

    bool m_ShowImagePanel = false;
    ImagePreview m_ImagePreview = NONE;

    VkDescriptorSet m_HeightmapDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet m_NormalmapDescriptorSet = VK_NULL_HANDLE;
};
