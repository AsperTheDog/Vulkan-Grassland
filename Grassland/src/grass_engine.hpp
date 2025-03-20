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

    struct TileBufferHeader
    {
		alignas(16) glm::uvec4 instanceOffsets;
        alignas(16) glm::uvec4 tileOffsets;
    };

    struct TileBufferElem
    {
        alignas(4) uint32_t globalTileIndex;
        alignas(4) uint32_t tileIndex;
    };

    struct InstanceElem
    {
        alignas(16) glm::vec3 position;
        alignas(4) float rotation;
        alignas(8) glm::vec2 uv;
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

    struct GrassPushConstantData
    {
        alignas(16) glm::mat4 vpMatrix;
        alignas(4) float widthMult = 0.7f;
        alignas(4) float tilt = 0.2f;
        alignas(4) float bend = 1.f;
        alignas(8) glm::vec2 windDir = {0.f, 1.f};
        alignas(4) float windStrength = 1.0f;
        alignas(16) glm::vec3 baseColor = { 0.0112f, 0.082f, 0.0f };
        alignas(16) glm::vec3 tipColor = { 0.25f, 0.6f, 0.0f };
        alignas(4) float colorRamp = 4.f;

        static uint32_t getVertexShaderOffset() { return offsetof(GrassPushConstantData, vpMatrix); }
        static uint32_t getFragmentShaderOffset() { return offsetof(GrassPushConstantData, baseColor); }

        static uint32_t getVertexShaderSize() { return getFragmentShaderOffset(); }
        static uint32_t getFragmentShaderSize() { return sizeof(GrassPushConstantData) - getFragmentShaderOffset(); }

        [[nodiscard]] const void* getVertexShaderData() const { return &vpMatrix; }
        [[nodiscard]] const void* getFragmentShaderData() const { return &baseColor; }
    };

    explicit GrassEngine(Engine& p_Engine) : m_Engine(p_Engine) {}

    void initalize(std::array<uint32_t, 4> p_TileGridSizes, std::array<uint32_t, 4> p_Densities);
    void initializeImgui();

    void cleanupImgui();

    void update(glm::ivec2 p_CameraTile, float p_HeightmapScale, float p_TileSize);

    void updateTileGridSize(std::array<uint32_t, 4> p_TileGridSizes);
    void updateGrassDensity(std::array<uint32_t, 4> p_NewDensities);

    void changeCurrentCenter(glm::ivec2 p_NewCenter, glm::vec2 p_Offset);
    void setDirty() { m_NeedsUpdate = true; }

    bool recompute(VulkanCommandBuffer& p_CmdBuffer, float p_TileSize, uint32_t p_GridSize, float p_HeightmapScale);
    bool recomputeWind(VulkanCommandBuffer& p_CmdBuffer);
    bool recomputeHeight(VulkanCommandBuffer& p_CmdBuffer);
    void render(const VulkanCommandBuffer& p_CmdBuffer);

    void drawImgui();

    bool transferCulling(VulkanCommandBuffer& p_CmdBuffer);

    [[nodiscard]] uint32_t getPreCullInstanceCount() const;
    [[nodiscard]] std::array<uint32_t, 4> getPreCullInstanceCounts() const;
    [[nodiscard]] uint32_t getPostCullInstanceCount() const;
    [[nodiscard]] std::array<uint32_t, 4> getPostCullInstanceCounts() const;
    [[nodiscard]] uint32_t getPreCullTileCount() const;
    [[nodiscard]] std::array<uint32_t,4> getPreCullTileCounts() const;
    [[nodiscard]] uint32_t getPostCullTileCount() const;
    [[nodiscard]] const std::array<uint32_t, 4>& getPostCullTileCounts() const { return m_PostCullTileCounts; }

    [[nodiscard]] bool isDirty() const { return m_NeedsUpdate || m_WindNoise.isNoiseDirty(); }

    bool m_RenderEnabled = true;

private:
    void recalculateCulling(float p_HeightmapScale, float p_TileSize);

    Engine& m_Engine;

    glm::ivec2 m_CurrentTile{ 0, 0 };

    std::array<uint32_t, 4> m_TileGridSizes{};
    std::array<uint32_t, 4> m_GrassDensities{};

    std::array<float, 4> m_GrassWidths{0.7f, 1.13f, 3.04f, 7.77f};

    glm::vec2 m_TileOffset{};
    glm::vec2 m_WindOffset{};

    bool m_NeedsUpdate = true;
    bool m_NeedsInstanceRebuild = true;
    bool m_NeedsTileRebuild = true;
    bool m_NeedsTransfer = true;

    std::vector<uint32_t> m_GlobalTilePositions{};
    std::vector<TileBufferElem> m_TileVisibilityData{};
    std::array<uint32_t, 4> m_PostCullTileCounts{};

    std::array<glm::vec3, 4> m_LODColors{};

    float m_CullingMargin = 0.f;

    GrassPushConstantData m_PushConstants{};

private:
    void rebuildInstanceResources();
    void rebuildTileResources();
    void recalculateGlobalTilesIndices();

    NoiseEngine::NoiseObject m_HeightNoise{};
    NoiseEngine::NoiseObject m_WindNoise{};

    ResourceID m_InstanceDataBufferID = UINT32_MAX;
    ResourceID m_TileDataBufferID = UINT32_MAX;

    ResourceID m_ComputePipelineLayoutID = UINT32_MAX;
    ResourceID m_ComputePipelineID = UINT32_MAX;
    ResourceID m_ComputeDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_ComputeDescriptorSetID = UINT32_MAX;

    ResourceID m_GrassPipelineLayoutID = UINT32_MAX;
    ResourceID m_GrassPipelineID = UINT32_MAX;
    ResourceID m_GrassDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_GrassDescriptorSetID = UINT32_MAX;

    VertexBufferData m_VertexBufferData{};

private:
    std::array<uint32_t, 4> m_ImguiGridSizes{};
    std::array<uint32_t, 4> m_ImguiGrassDensities{};

    float m_ImguiGrassBaseHeight = 3.f;
    float m_ImguiGrassHeightVariation = 2.f;
    float m_ImguiWindDirection = 0.f;
    float m_ImguiWindSpeed = 0.04f;

    bool m_ImguiWAnimated = true;
    float m_ImguiWindWSpeed = 0.6f;

    float m_ImguiCullingMargin = 3.f;

    bool m_RandomizeLODColors = false;
    bool m_CullingEnable = true;
    bool m_CullingUpdate = true;
    bool m_NeedsCullingUpdate = true;

    //Debug
    uint32_t m_DebugInstanceBufferSize = 0;
    uint32_t m_DebugTileBufferSize = 0;
    uint32_t m_DebugComputeThreads = 0;
    std::array<uint32_t, 4> m_DebugInstanceCalls;
    std::array<uint32_t, 4> m_DebugInstanceOffsets;
    TileBufferHeader m_DebugTileHeader{};
};

