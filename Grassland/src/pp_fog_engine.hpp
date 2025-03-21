#pragma once
#include <glm/glm.hpp>

#include "utils/identifiable.hpp"
class VulkanCommandBuffer;
class Engine;

class PPFogEngine
{
public:
    struct PushConstantData
    {
        alignas(16) glm::vec3 fogColor{0.6f, 0.7f, 0.8f};
        alignas(4) float fogDensity = 0.0015f;
        alignas(4) float nearPlane;
        alignas(4) float farPlane;
    };

    explicit PPFogEngine(Engine& p_Engine) : m_Engine(p_Engine) {}

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

    ResourceID m_PPFogPipelineID = UINT32_MAX;
    ResourceID m_PPFogPipelineLayoutID = UINT32_MAX;
    ResourceID m_PPFogDescriptorSetLayoutID = UINT32_MAX;
    ResourceID m_PPFogDescriptorSetID = UINT32_MAX;
};

