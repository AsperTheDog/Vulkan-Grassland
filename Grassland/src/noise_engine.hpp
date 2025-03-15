#pragma once
#include <glm/glm.hpp>
#include <Volk/volk.h>

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

        NoisePushConstantData noisePushConstants{};
        NormalPushConstantData normalPushConstants{};

        glm::uvec2 size{};
        
        ResourceID computeNoiseDescriptorSetID = UINT32_MAX;
        ResourceID computeNormalDescriptorSetID = UINT32_MAX;

        VkDescriptorSet imguiHeightmapDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet imguiNormalmapDescriptorSet = VK_NULL_HANDLE;


        void initialize(uint32_t p_Size, Engine& p_Engine, bool p_IncludeNormal);
        void initializeImgui(Engine& p_Engine);

        void cleanup();

    private:
        NoiseEngine* m_NoiseEngine = nullptr;

        friend class NoiseEngine;
    };

public:
    explicit NoiseEngine(Engine& p_Engine) : m_Engine(p_Engine) {}

    void initialize();
    void initializeImgui() const;

    [[nodiscard]] Engine& getEngine() const { return m_Engine; }

    [[nodiscard]] NoiseObject* getImguiObject() const { return m_CurrentNoiseImgui; }

    void drawImgui();

    void setImguiObject(NoiseObject* p_Object) { m_CurrentNoiseImgui = p_Object; }
    
    void updateNoise(const VulkanCommandBuffer& p_CmdBuffer, const NoiseObject& p_Object) const;
    void updateNormal(const VulkanCommandBuffer& p_CmdBuffer, const NoiseObject& p_Object) const;

private:
    Engine& m_Engine;

    ResourceID m_ComputeNoisePipelineID = UINT32_MAX;
    ResourceID m_ComputeNormalPipelineID = UINT32_MAX;
    ResourceID m_ComputeNoisePipelineLayoutID = UINT32_MAX;
    ResourceID m_ComputeNormalPipelineLayoutID = UINT32_MAX;
    ResourceID m_ComputeNoiseDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_ComputeNormalDescriptorSetLayoutID = UINT32_MAX;

private:
    NoiseObject* m_CurrentNoiseImgui = nullptr;

    enum ImagePreview : uint8_t
    {
        HEIGHTMAP,
        NORMALMAP
    } m_ImagePreview = HEIGHTMAP;

    friend struct NoiseObject;
};

