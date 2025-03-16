#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <Volk/volk.h>

#include "noise_engine.hpp"
#include "utils/identifiable.hpp"

class VulkanCommandBuffer;
class Engine;

class PlaneEngine
{
public:
    struct PushConstantData
    {
        alignas(4)  uint32_t gridSize = 30;
        alignas(4)  float patchSize = 20.f;
        alignas(8)  glm::vec2 cameraTile;
        alignas(16) glm::vec3 cameraPos;
        alignas(4)  float minTessLevel = 3.f;
        alignas(4)  float maxTessLevel = 64.f;
        alignas(4)  float tessFactor = 0.002f;
        alignas(4)  float tessSlope = 0.05f;
        alignas(4)  float heightScale = 15.f;
        alignas(16) glm::mat4 mvp;
        alignas(16) glm::vec3 color = { 0.0f, 0.15f, 0.0f };

        static uint32_t getVertexShaderOffset() { return offsetof(PushConstantData, gridSize); }
        static uint32_t getTessellationControlShaderOffset() { return offsetof(PushConstantData, cameraPos); }
        static uint32_t getTessellationEvaluationShaderOffset() { return offsetof(PushConstantData, heightScale); }
        static uint32_t getFragmentShaderOffset() { return offsetof(PushConstantData, color); }

        static uint32_t getVertexShaderSize() { return getTessellationControlShaderOffset(); }
        static uint32_t getTessellationControlShaderSize() { return getTessellationEvaluationShaderOffset() - getTessellationControlShaderOffset(); }
        static uint32_t getTessellationEvaluationShaderSize() { return getFragmentShaderOffset() - getTessellationEvaluationShaderOffset(); }
        static uint32_t getFragmentShaderSize() { return sizeof(PushConstantData) - getFragmentShaderOffset(); }

        [[nodiscard]] const void* getVertexShaderData() const { return &gridSize; }
        [[nodiscard]] const void* getTessellationControlShaderData() const { return &cameraPos; }
        [[nodiscard]] const void* getTessellationEvaluationShaderData() const { return &heightScale; }
        [[nodiscard]] const void* getFragmentShaderData() const { return &color; }
    };

public:
    explicit PlaneEngine(Engine& p_Engine) : m_Engine(p_Engine) {}

    void initialize();
    void initializeImgui();

    void update(glm::vec2 p_CamTile);
    void render(const VulkanCommandBuffer& p_CmdBuffer) const;

    void cleanup();

    void drawImgui();

    [[nodiscard]] float getTileSize() const { return m_PushConstants.patchSize; }
    [[nodiscard]] float getGridExtent() const { return m_PushConstants.patchSize * m_PushConstants.gridSize; }
    [[nodiscard]] float getHeightScale() const { return m_PushConstants.heightScale; }
    [[nodiscard]] glm::vec2 getCameraTile() const { return m_PushConstants.cameraTile; }
    [[nodiscard]] uint32_t getGridSize() const { return m_PushConstants.gridSize; }

private:
    void createPipelines();
    void createHeightmapDescriptorSets();

    Engine& m_Engine;

private:
    ResourceID m_TessellationPipelineID = UINT32_MAX;
    ResourceID m_TessellationPipelineWFID = UINT32_MAX;
    ResourceID m_TessellationPipelineLayoutID = UINT32_MAX;
    ResourceID m_TessellationDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_TessellationDescriptorSetID = UINT32_MAX;

private:
    PushConstantData m_PushConstants{};

    bool m_Wireframe = false;
};

