#include "pp_fog_engine.hpp"

#include "engine.hpp"

#include <vulkan_device.hpp>
#include "ext/vulkan_swapchain.hpp"

void PPFogEngine::update()
{
    m_PushConstants.nearPlane = m_Engine.getCamera().getNearPlane();
    m_PushConstants.farPlane = m_Engine.getCamera().getFarPlane();
}

void PPFogEngine::initialize()
{
    VulkanDevice& l_Device = m_Engine.getDevice();

    {
        std::array<VkDescriptorSetLayoutBinding, 2> l_Bindings;
        l_Bindings[0].binding = 0;
        l_Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        l_Bindings[0].descriptorCount = 1;
        l_Bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_Bindings[0].pImmutableSamplers = nullptr;
        l_Bindings[1].binding = 1;
        l_Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        l_Bindings[1].descriptorCount = 1;
        l_Bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_Bindings[1].pImmutableSamplers = nullptr;

        m_PPFogDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
    }

    m_PPFogDescriptorSetID = l_Device.createDescriptorSet(m_Engine.getDescriptorPoolID(), m_PPFogDescriptorSetLayoutID);

    {
        std::array<VkPushConstantRange, 1> l_PushConstantRanges;
        l_PushConstantRanges[0] = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData) };
        std::array<ResourceID, 1> l_DescriptorSetLayouts = { m_PPFogDescriptorSetLayoutID };
        m_PPFogPipelineLayoutID = l_Device.createPipelineLayout(l_DescriptorSetLayouts, l_PushConstantRanges);
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
    const uint32_t fragmentShaderID = l_Device.createShader("shaders/fog.frag", VK_SHADER_STAGE_FRAGMENT_BIT, false, {});

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

    m_PPFogPipelineID = l_Device.createPipeline(l_SkyboxBuilder, m_PPFogPipelineLayoutID, m_Engine.getRenderPassID(), 1);

    {
        const VkDescriptorImageInfo l_RenderImageInfo{
            .sampler = VK_NULL_HANDLE,
            .imageView = *l_Device.getImage(m_Engine.getRenderImage()).getImageView(m_Engine.getRenderImageView()),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkDescriptorImageInfo l_DepthImageInfo{
            .sampler = VK_NULL_HANDLE,
            .imageView = *l_Device.getImage(m_Engine.getDepthBuffer()).getImageView(m_Engine.getDepthBufferView()),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        std::array<VkWriteDescriptorSet, 2> l_DescriptorWrite{};
    
        l_DescriptorWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_DescriptorWrite[0].dstSet = *l_Device.getDescriptorSet(m_PPFogDescriptorSetID);
        l_DescriptorWrite[0].dstBinding = 0;
        l_DescriptorWrite[0].dstArrayElement = 0;
        l_DescriptorWrite[0].descriptorCount = 1;
        l_DescriptorWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        l_DescriptorWrite[0].pImageInfo = &l_RenderImageInfo;
    
        l_DescriptorWrite[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_DescriptorWrite[1].dstSet = *l_Device.getDescriptorSet(m_PPFogDescriptorSetID);
        l_DescriptorWrite[1].dstBinding = 1;
        l_DescriptorWrite[1].dstArrayElement = 0;
        l_DescriptorWrite[1].descriptorCount = 1;
        l_DescriptorWrite[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        l_DescriptorWrite[1].pImageInfo = &l_DepthImageInfo;

        l_Device.updateDescriptorSets(l_DescriptorWrite);
    }
}

void PPFogEngine::render(const VulkanCommandBuffer& p_CmdBuffer) const
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

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, m_PPFogPipelineID);
    p_CmdBuffer.cmdSetViewport(viewport);
    p_CmdBuffer.cmdSetScissor(scissor);

    p_CmdBuffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, m_PPFogPipelineLayoutID, m_PPFogDescriptorSetID);
    p_CmdBuffer.cmdPushConstant(m_PPFogPipelineLayoutID, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &m_PushConstants);
    p_CmdBuffer.cmdDraw(3, 0);
}

void PPFogEngine::drawImgui()
{
    ImGui::Begin("Fog");

    ImGui::ColorEdit3("Fog Color", &m_PushConstants.fogColor.x);
    float l_FogDensity = m_PushConstants.fogDensity * 100.f;
    ImGui::DragFloat("Fog Density", &l_FogDensity, 0.01f, 0.0f, 1.0f);
    m_PushConstants.fogDensity = l_FogDensity / 100.f;

    ImGui::End();
}
