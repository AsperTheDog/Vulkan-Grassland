#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <Volk/volk.h>

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
        alignas(16) glm::vec3 color = { 0.0f, 0.5f, 0.0f };

        static uint32_t getVertexShaderOffset() { return 0; }
        static uint32_t getTessellationControlShaderOffset() { return offsetof(PushConstantData, cameraPos); }
        static uint32_t getTessellationEvaluationShaderOffset() { return offsetof(PushConstantData, heightScale); }
        static uint32_t getFragmentShaderOffset() { return offsetof(PushConstantData, color); }

        static uint32_t getVertexShaderSize() { return offsetof(PushConstantData, cameraPos); }
        static uint32_t getTessellationControlShaderSize() { return offsetof(PushConstantData, heightScale) - offsetof(PushConstantData, cameraPos); }
        static uint32_t getTessellationEvaluationShaderSize() { return offsetof(PushConstantData, color) - offsetof(PushConstantData, heightScale); }
        static uint32_t getFragmentShaderSize() { return sizeof(PushConstantData) - offsetof(PushConstantData, color); }
    };

    struct NoisePushConstantData
    {
        alignas(8) glm::vec2 offset;
        alignas(8) glm::uvec2 size;
        alignas(4) float w;
        alignas(4) float scale = 4.f;
    };

    struct NormalPushConstantData
    {
        alignas(4) float heightScale = 10.f;
        alignas(4) float offsetScale = 0.01f;
        alignas(4) float patchSize = 1.f;
        alignas(4) uint32_t gridSize = 100;
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

    [[nodiscard]] ResourceID getHeightmapID() const { return m_HeightmapID; }
    [[nodiscard]] ResourceID getHeightmapViewID() const { return m_HeightmapViewID; }
    [[nodiscard]] ResourceID getHeightmapSamplerID() const { return m_HeightmapSamplerID; }

    [[nodiscard]] ResourceID getNormalmapID() const { return m_NormalmapID; }
    [[nodiscard]] ResourceID getNormalmapViewID() const { return m_NormalmapViewID; }
    [[nodiscard]] ResourceID getNormalmapSamplerID() const { return m_NormalmapSamplerID; }

    [[nodiscard]] float getTileSize() const { return m_PushConstants.patchSize; }
    [[nodiscard]] float getGridExtent() const { return m_PushConstants.patchSize * m_PushConstants.gridSize; }
    [[nodiscard]] float getHeightmapScale() const { return m_PushConstants.heightScale; }
    [[nodiscard]] glm::vec2 getCameraTile() const { return m_PushConstants.cameraTile; }

private:
    void createPipelines();
    void createHeightmapDescriptorSets(uint32_t p_TextWidth, uint32_t p_TextHeight);

    Engine& m_Engine;

private:
    ResourceID m_HeightmapID = UINT32_MAX;
    ResourceID m_HeightmapViewID = UINT32_MAX;
    ResourceID m_HeightmapSamplerID = UINT32_MAX;

    ResourceID m_NormalmapID = UINT32_MAX;
    ResourceID m_NormalmapViewID = UINT32_MAX;
    ResourceID m_NormalmapSamplerID = UINT32_MAX;

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

private:
    PushConstantData m_PushConstants{};

    NoisePushConstantData m_NoisePushConstants{};
    NormalPushConstantData m_NormalPushConstants{};
    glm::vec2 m_NoiseOffset = { 0.0f, 0.0f };
    bool m_NoiseHotReload = true;
    bool m_NormalHotReload = true;
    bool m_WAnimated = false;
    float m_WSpeed = 0.1f;
    float m_WOffset = 0.0f;
    float m_W = 0.0f;

    bool m_Wireframe = false;

    bool m_ShowImagePanel = false;

    enum ImagePreview : uint8_t
    {
        NONE,
        HEIGHTMAP,
        NORMALMAP
    } m_ImagePreview = NONE;

    VkDescriptorSet m_HeightmapDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet m_NormalmapDescriptorSet = VK_NULL_HANDLE;
};

