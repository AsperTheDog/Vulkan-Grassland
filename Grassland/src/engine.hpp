#pragma once
#include <utils/identifiable.hpp>

#include "camera.hpp"
#include "imgui.h"
#include "sdl_window.hpp"
#include "vulkan_queues.hpp"

struct PushConstantData
{
    alignas(4)  uint32_t gridSize = 100;
    alignas(4)  float patchSize = 1.f;
    alignas(4)  float minTessLevel = 1.f;
    alignas(4)  float maxTessLevel = 32.f;
    alignas(4)  float tessFactor = 0.014f;
    alignas(4)  float tessSlope = 0.07f;
    alignas(16) glm::vec3 cameraPos;
    alignas(4)  float heightScale = 10.f;
    alignas(16) glm::mat4 mvp;
    alignas(4)  float uvOffsetScale = 0.002f;

    static uint32_t getVertexShaderOffset() { return 0; }
    static uint32_t getTessellationControlShaderOffset() { return offsetof(PushConstantData, minTessLevel); }
    static uint32_t getTessellationEvaluationShaderOffset() { return offsetof(PushConstantData, heightScale); }

    static uint32_t getVertexShaderSize() { return offsetof(PushConstantData, minTessLevel); }
    static uint32_t getTessellationControlShaderSize() { return offsetof(PushConstantData, heightScale) - offsetof(PushConstantData, minTessLevel); }
    static uint32_t getTessellationEvaluationShaderSize() { return sizeof(PushConstantData) - offsetof(PushConstantData, heightScale); }
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
    void createHeightmapDescriptor();

    void render(VulkanCommandBuffer& p_Buffer, uint32_t l_ImageIndex, ImDrawData* p_ImGuiDrawData);

    void recreateSwapchain(VkExtent2D p_NewSize);

    SDLWindow m_Window;

    Camera m_Camera;

    QueueSelection m_GraphicsQueuePos;
    QueueSelection m_PresentQueuePos;
    QueueSelection m_TransferQueuePos;

    ResourceID m_DeviceID = UINT32_MAX;

    ResourceID m_SwapchainID = UINT32_MAX;

    ResourceID m_GraphicsCmdBufferID = UINT32_MAX;

    ResourceID m_DepthBuffer = UINT32_MAX;
    ResourceID m_DepthBufferView = UINT32_MAX;
	std::vector<ResourceID> m_FramebufferIDs{};

    ResourceID m_RenderPassID = UINT32_MAX;

    ResourceID m_GraphicsPipelineID = UINT32_MAX;
    ResourceID m_PlanePipelineID = UINT32_MAX;

    ResourceID m_RenderFinishedSemaphoreID = UINT32_MAX;
    ResourceID m_InFlightFenceID = UINT32_MAX;

    ResourceID m_HeightmapID = UINT32_MAX;
    ResourceID m_HeightmapViewID = UINT32_MAX;
    ResourceID m_HeightmapSamplerID = UINT32_MAX;

    ResourceID m_TessellationPipelineID = UINT32_MAX;
    ResourceID m_TessellationPipelineLayoutID = UINT32_MAX;
    ResourceID m_TessellationDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_TessellationDescriptorPoolID = UINT32_MAX;
    ResourceID m_TessellationDescriptorSetID = UINT32_MAX;

    PushConstantData m_PushConstants;
    float m_UVOffset = 0.005f;

private:
    void initImgui() const;
    void drawImgui();
};
