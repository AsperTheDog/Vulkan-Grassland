#pragma once
#include <cstdint>

#include <glm/glm.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <array>
#include <glm/gtx/hash.hpp>

#include "utils/identifiable.hpp"

class VulkanCommandBuffer;
class Engine;

class GrassEngine
{
public:
    struct InstanceElem
    {
        alignas(16) glm::vec3 position;
        alignas(4) float rotation;
    };

    struct ComputePushConstantData
    {
        alignas(8) glm::vec2 centerPos;
        alignas(8) glm::vec2 worldOffset;
        alignas(16) glm::uvec3 tileGridSizes;
        alignas(16) glm::uvec3 tileDensities;
        alignas(4) float tileSize;
        alignas(4) float gridExtent;
        alignas(4) float heightmapScale;
    };

    struct ImageData
    {
        ResourceID image = UINT32_MAX;
        ResourceID view = UINT32_MAX;
        ResourceID sampler = UINT32_MAX;
    };

    explicit GrassEngine(Engine& p_Engine) : m_Engine(p_Engine) {}

    void initalize(ImageData p_Heightmap, std::array<uint32_t, 3> p_TileGridSizes, std::array<uint32_t, 3> p_Densities);
    void initializeImgui();

    void update(glm::vec2 p_CameraTile);

    void updateTileGridSize(std::array<uint32_t, 3> p_TileGridSizes);
    void updateGrassDensity(std::array<uint32_t, 3> p_NewDensities);

    void changeCurrentCenter(const glm::ivec2& p_NewCenter);

    void recompute(const VulkanCommandBuffer& p_CmdBuffer, float p_TileSize, float p_GridExtent, float p_HeightmapScale, uint32_t p_GraphicsQueueFamilyIndex);
    void render(const VulkanCommandBuffer& p_CmdBuffer) const;

    void drawImgui();

    uint32_t getInstanceCount() const;

private:
    Engine& m_Engine;

    glm::vec2 m_CurrentTile{ 0, 0 };

    std::array<uint32_t, 3> m_TileGridSizes;
    std::array<uint32_t, 3> m_GrassDensities;

    std::array<uint32_t, 3> m_ImguiGridSizes;
    std::array<uint32_t, 3> m_ImguiGrassDensities;

    bool m_NeedsRebuild = true;

private:
    void rebuildResources();

    ImageData m_HeightmapID{};

    ResourceID m_InstanceDataBufferID = UINT32_MAX;

    ResourceID m_ComputePipelineLayoutID = UINT32_MAX;
    ResourceID m_ComputePipelineID = UINT32_MAX;
    ResourceID m_ComputeDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_ComputeDescriptorSetID = UINT32_MAX;

    ResourceID m_GrassPipelineLayoutID = UINT32_MAX;
    ResourceID m_GrassPipelineID = UINT32_MAX;
};

