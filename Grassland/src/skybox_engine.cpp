#include "skybox_engine.hpp"

#include "engine.hpp"
#include "vulkan_device.hpp"
#include "ext/vulkan_swapchain.hpp"

void SkyboxEngine::update()
{
    m_PushConstants.invVPMatrix = m_Engine.getCamera().getInvVPMatrix();
    m_PushConstants.lightDir = m_Engine.getLightDir();
}

void SkyboxEngine::initialize()
{
    VulkanDevice& l_Device = m_Engine.getDevice();

    {
        std::array<VkPushConstantRange, 1> l_PushConstantRanges;
        l_PushConstantRanges[0] = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData) };
        m_SkyboxPipelineLayoutID = l_Device.createPipelineLayout({}, l_PushConstantRanges);
    }

    VkPipelineColorBlendAttachmentState l_ColorBlendAttachment;
    l_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    l_ColorBlendAttachment.blendEnable = VK_FALSE;
    l_ColorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    l_ColorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    l_ColorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    l_ColorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    l_ColorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    l_ColorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
     
    std::array<VkDynamicState, 2> l_DynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    const uint32_t vertexShaderID = l_Device.createShader("shaders/quad.vert", VK_SHADER_STAGE_VERTEX_BIT, false, {});
    const uint32_t fragmentShaderID = l_Device.createShader("shaders/skybox.frag", VK_SHADER_STAGE_FRAGMENT_BIT, false, {});

    VulkanPipelineBuilder l_SkyboxBuilder{l_Device.getID()};
    l_SkyboxBuilder.setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
    l_SkyboxBuilder.setViewportState(1, 1);
    l_SkyboxBuilder.setRasterizationState(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
    l_SkyboxBuilder.setMultisampleState(VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 1.0f);
    l_SkyboxBuilder.setDepthStencilState(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS);
    l_SkyboxBuilder.addColorBlendAttachment(l_ColorBlendAttachment);
    l_SkyboxBuilder.setColorBlendState(VK_FALSE, VK_LOGIC_OP_COPY, { 0.0f, 0.0f, 0.0f, 0.0f });
    l_SkyboxBuilder.setDynamicState(l_DynamicStates);
    l_SkyboxBuilder.addShaderStage(vertexShaderID, "main");
    l_SkyboxBuilder.addShaderStage(fragmentShaderID, "main");

    m_SkyboxPipelineID = l_Device.createPipeline(l_SkyboxBuilder, m_SkyboxPipelineLayoutID, m_Engine.getRenderPassID(), 0);
}

void SkyboxEngine::render(const VulkanCommandBuffer& p_CmdBuffer) const
{
    const VkExtent2D& extent = m_Engine.getSwapchain().getExtent();

    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset = { 0, 0 };
    scissor.extent = extent;

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyboxPipelineID);
    p_CmdBuffer.cmdSetViewport(viewport);
    p_CmdBuffer.cmdSetScissor(scissor);

    p_CmdBuffer.cmdPushConstant(m_SkyboxPipelineLayoutID, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &m_PushConstants);
    p_CmdBuffer.cmdDraw(3, 0);
}

void SkyboxEngine::drawImgui()
{
    ImGui::Begin("Skybox");

    ImGui::ColorEdit3("Sky top color", &m_PushConstants.skyTopColor.x);
    ImGui::ColorEdit3("Sky horizon color", &m_PushConstants.skyHorizonColor.x);
    ImGui::DragFloat("Horizon falloff", &m_PushConstants.horizonFalloff, 0.01f, 0.0f, 1.0f);

    ImGui::Separator();

    ImGui::ColorEdit3("Sun color", &m_PushConstants.sunColor.x);
    ImGui::DragFloat("Sun size", &m_PushConstants.sunSize, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Sun intensity", &m_PushConstants.sunIntensity, 1.f, 0.0f, 100.0f);
    ImGui::DragFloat("Sun glow falloff", &m_PushConstants.sunGlowFalloff, 10.f, 0.0f, 1000.0f);
    ImGui::DragFloat("Sun glow intensity", &m_PushConstants.sunGlowIntensity, 0.1f, 0.0f, 10.0f);

    ImGui::Separator();

    ImGui::ColorEdit3("Fog color", &m_PushConstants.fogColor.x);
    ImGui::DragFloat("Fog strength", &m_PushConstants.fogStrength, 0.01f, 0.0f, 1.0f);

    ImGui::Separator();

    ImGui::DragFloat("Exposure", &m_PushConstants.exposure, 0.01f, 0.0f, 1.0f);

    ImGui::End();
}
