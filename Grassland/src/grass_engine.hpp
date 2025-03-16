#pragma once
#include <cstdint>

#include <glm/glm.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <array>
#include <glm/gtx/hash.hpp>

#include "noise_engine.hpp"
#include "utils/identifiable.hpp"

class VulkanCommandBuffer;
class Engine;

class GrassEngine
{
    struct VertexBufferData
    {
        ResourceID m_LODBuffer = UINT32_MAX;

        uint32_t m_IndexStart = 0;
        std::array<uint32_t, 4> m_IndexOffsets{};
        std::array<uint32_t, 4> m_IndexCounts{};
    };

public:
    using ImageData = NoiseEngine::NoiseObject::ImageData;

    struct InstanceElem
    {
        alignas(16) glm::vec3 position;
        alignas(4) float rotation;
        alignas(4) float height;
    };

    struct ComputePushConstantData
    {
        alignas(8) glm::vec2 centerPos;
        alignas(8) glm::vec2 worldOffset;
        alignas(16) glm::uvec4 tileGridSizes;
        alignas(16) glm::uvec4 tileDensities;
        alignas(4) float tileSize;
        alignas(4) float gridExtent;
        alignas(4) float heightmapScale;
        alignas(4) float grassBaseHeight;
        alignas(4) float grassHeightVariation;
    };

    explicit GrassEngine(Engine& p_Engine) : m_Engine(p_Engine) {}

    void initalize(std::array<uint32_t, 4> p_TileGridSizes, std::array<uint32_t, 4> p_Densities);
    void initializeImgui();

    void update(glm::vec2 p_CameraTile);

    void updateTileGridSize(std::array<uint32_t, 4> p_TileGridSizes);
    void updateGrassDensity(std::array<uint32_t, 4> p_NewDensities);

    void changeCurrentCenter(glm::ivec2 p_NewCenter, glm::vec2 p_GridExtent);
    void setDirty() { m_NeedsUpdate = true; }

    void recompute(const VulkanCommandBuffer& p_CmdBuffer, float p_TileSize, float p_GridExtent, float p_HeightmapScale, uint32_t p_GraphicsQueueFamilyIndex);
    void render(const VulkanCommandBuffer& p_CmdBuffer) const;

    void drawImgui();

    [[nodiscard]] uint32_t getInstanceCount() const;
    [[nodiscard]] std::array<uint32_t, 4> getInstanceCounts() const;
    [[nodiscard]] bool isDirty() const { return m_NeedsUpdate; }

private:
    Engine& m_Engine;

    glm::vec2 m_CurrentTile{ 0, 0 };

    std::array<uint32_t, 4> m_TileGridSizes;
    std::array<uint32_t, 4> m_GrassDensities;

    std::array<uint32_t, 4> m_ImguiGridSizes;
    std::array<uint32_t, 4> m_ImguiGrassDensities;

    float m_ImguiGrassBaseHeight = 1.5f;
    float m_ImguiGrassHeightVariation = 1.f;

    bool m_NeedsUpdate = true;
    bool m_NeedsRebuild = true;

private:
    void rebuildResources();

    NoiseEngine::NoiseObject m_HeightNoise{};

    ResourceID m_InstanceDataBufferID = UINT32_MAX;

    ResourceID m_ComputePipelineLayoutID = UINT32_MAX;
    ResourceID m_ComputePipelineID = UINT32_MAX;
    ResourceID m_ComputeDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_ComputeDescriptorSetID = UINT32_MAX;

    ResourceID m_GrassPipelineLayoutID = UINT32_MAX;
    ResourceID m_GrassPipelineID = UINT32_MAX;

    VertexBufferData m_VertexBufferData{};
};

