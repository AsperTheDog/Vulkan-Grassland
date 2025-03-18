#pragma once
#include <__msvc_string_view.hpp>
#include <glm/glm.hpp>
#include <Volk/volk.h>

#include "vulkan_queues.hpp"
#include "utils/identifiable.hpp"

class Engine;
class VulkanCommandBuffer;

class NoiseEngine
{
public:
    struct NoisePushConstantData
    {
        alignas(8) glm::vec2 offset;
        alignas(8) glm::uvec2 size;
        alignas(4) float w;
        alignas(4) float scale = 4.f;
        alignas(4) uint32_t octaves = 3;
        alignas(4) float persistence = 5.f;
        alignas(4) float lacunarity = 3.f;
    };

    struct NormalPushConstantData
    {
        alignas(4) float heightScale = 10.f;
        alignas(4) float offsetScale = 0.01f;
        alignas(4) float patchSize = 1.f;
        alignas(4) uint32_t gridSize = 100;
    };

    struct NoiseObject
    {
        struct ImageData
        {
            ResourceID image = UINT32_MAX;
            ResourceID view = UINT32_MAX;
            ResourceID sampler = UINT32_MAX;
        };

        ImageData noiseImage{};
        ImageData normalImage{};

        bool includeNormal = false;

        bool noiseHotReload = true;
        bool noiseNeedsRebuild = true;
        bool normalHotReload = true;
        bool normalNeedsRebuild = true;

        NoisePushConstantData noisePushConstants{};
        NormalPushConstantData normalPushConstants{};

        glm::uvec2 size{};
        
        ResourceID computeNoiseDescriptorSetID = UINT32_MAX;
        ResourceID computeNormalDescriptorSetID = UINT32_MAX;

        VkDescriptorSet imguiHeightmapDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet imguiNormalmapDescriptorSet = VK_NULL_HANDLE;

        void initialize(uint32_t p_Size, Engine& p_Engine, bool p_IncludeNormal);
        void initializeImgui();

        [[nodiscard]] bool isNoiseDirty() const { return noiseNeedsRebuild; }
        [[nodiscard]] bool isNormalDirty() const { return normalNeedsRebuild && includeNormal; }
        [[nodiscard]] bool isDirty() const { return isNoiseDirty() || isNormalDirty(); }

        void updatePatchSize(float p_PatchSize);
        void updateGridSize(uint32_t p_GridSize);
        void updateHeightScale(float p_HeightScale);
        void updateOffset(glm::vec2 p_Offset);
        void shiftOffset(glm::vec2 p_Offset);
        void shiftW(float p_W);

        void drawImgui(std::string_view p_NoiseName);

        void cleanupImgui();
        void toggleImgui() { m_ShowWindow = !m_ShowWindow; }

        void overridePushConstant(const NoisePushConstantData& p_NewPush) { noisePushConstants = p_NewPush; }

    private:
        NoiseEngine* m_NoiseEngine = nullptr;

        bool m_ShowWindow = false;

        glm::vec2 m_NoiseOffset = { 0.0f, 0.0f };
        bool m_WAnimated = false;
        float m_WSpeed = 0.1f;
        float m_WOffset = 0.0f;
        float m_W = 0.0f;
        
        enum ImagePreview : uint8_t
        {
            HEIGHTMAP,
            NORMALMAP
        } m_ImagePreview = HEIGHTMAP;

        friend class NoiseEngine;
    };

public:
    explicit NoiseEngine(Engine& p_Engine) : m_Engine(p_Engine) {}

    void initialize();
    void initializeImgui() const {}

    void cleanupImgui() const {}

    [[nodiscard]] Engine& getEngine() const { return m_Engine; }

    void drawImgui() const {}

    bool recalculate(VulkanCommandBuffer& p_CmdBuffer, NoiseObject& p_Object) const;

private:
    bool recalculateNoise(VulkanCommandBuffer& p_CmdBuffer, NoiseObject& p_Object) const;
    bool recalculateNormal(VulkanCommandBuffer& p_CmdBuffer, NoiseObject& p_Object) const;
    Engine& m_Engine;

    ResourceID m_ComputeNoisePipelineID = UINT32_MAX;
    ResourceID m_ComputeNormalPipelineID = UINT32_MAX;
    ResourceID m_ComputeNoisePipelineLayoutID = UINT32_MAX;
    ResourceID m_ComputeNormalPipelineLayoutID = UINT32_MAX;
    ResourceID m_ComputeNoiseDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_ComputeNormalDescriptorSetLayoutID = UINT32_MAX;

    ResourceID m_NoiseComputeCmdBufferID = UINT32_MAX;

private:
    friend struct NoiseObject;
};

