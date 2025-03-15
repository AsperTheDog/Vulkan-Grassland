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
        alignas(4)  float heightScale = 10.f;
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

    void initialize(uint32_t p_ImgSize);
    void initializeImgui();

    void update();
    void render(const VulkanCommandBuffer& p_CmdBuffer) const;

    void cleanup();

    void drawImgui();

    void updateNoise(const VulkanCommandBuffer& p_CmdBuffer) const;
    void updateNormal(const VulkanCommandBuffer& p_CmdBuffer) const;

    [[nodiscard]] NoiseEngine::NoiseObject& getNoise() { return m_Noise; }

    [[nodiscard]] float getTileSize() const { return m_PushConstants.patchSize; }
    [[nodiscard]] float getGridExtent() const { return m_PushConstants.patchSize * m_PushConstants.gridSize; }
    [[nodiscard]] float getHeightmapScale() const { return m_PushConstants.heightScale; }
    [[nodiscard]] glm::vec2 getCameraTile() const { return m_PushConstants.cameraTile; }

private:
    void createPipelines();
    void createHeightmapDescriptorSets(uint32_t p_TextureSize);

    Engine& m_Engine;

private:
    NoiseEngine::NoiseObject m_Noise{};

    ResourceID m_TessellationPipelineID = UINT32_MAX;
    ResourceID m_TessellationPipelineWFID = UINT32_MAX;
    ResourceID m_TessellationPipelineLayoutID = UINT32_MAX;
    ResourceID m_TessellationDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_TessellationDescriptorSetID = UINT32_MAX;

private:
    PushConstantData m_PushConstants{};

    glm::vec2 m_NoiseOffset = { 0.0f, 0.0f };
    bool m_NoiseHotReload = true;
    bool m_NormalHotReload = true;
    bool m_WAnimated = false;
    float m_WSpeed = 0.1f;
    float m_WOffset = 0.0f;
    float m_W = 0.0f;

    bool m_Wireframe = false;
};

