#pragma once
#include <glm/glm.hpp>

#include "vulkan_command_buffer.hpp"
#include "utils/identifiable.hpp"
class Engine;

class SkyboxEngine
{
public:
    struct PushConstantData
    {
        alignas(16) glm::mat4 invVPMatrix;
        alignas(16) glm::vec3 lightDir;
        alignas(16) glm::vec3 skyTopColor{0.02f, 0.439f, 0.824f};
        alignas(16) glm::vec3 skyHorizonColor{0.29f, 0.718f, 0.855f};
        alignas(4) float horizonFalloff = 0.5f;
        alignas(16) glm::vec3 fogColor{0.361f, 0.525f, 0.533f};
        alignas(4) float fogStrength = 0.01f;
        alignas(16) glm::vec3 sunColor{1.f, 0.95f, 0.f};
        alignas(4) float sunSize = 0.02f;
        alignas(4) float sunIntensity = 20.f;
        alignas(4) float sunGlowFalloff = 400.f;
        alignas(4) float sunGlowIntensity = 6.f;
        alignas(4) float exposure = 1.f;
    };

    explicit SkyboxEngine(Engine& p_Engine) : m_Engine(p_Engine) {}

    void update();

    void initialize();
    void initializeImgui() const {}

    void render(const VulkanCommandBuffer& p_CmdBuffer) const;

    void drawImgui();

    void cleanupImgui() const {}

private:
    PushConstantData m_PushConstants{};

private:
    Engine& m_Engine;

    ResourceID m_SkyboxPipelineID = UINT32_MAX;
    ResourceID m_SkyboxPipelineLayoutID = UINT32_MAX;
};

