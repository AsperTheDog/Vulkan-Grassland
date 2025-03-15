#include "plane_engine.hpp"

#include "engine.hpp"
#include "imgui.h"
#include "vulkan_device.hpp"
#include "backends/imgui_impl_vulkan.h"
#include "ext/vulkan_swapchain.hpp"
#include "utils/logger.hpp"

void PlaneEngine::initialize(const uint32_t p_ImgSize)
{
    createHeightmapDescriptorSets(p_ImgSize);
    createPipelines();
}

void PlaneEngine::initializeImgui()
{
    m_Noise.initializeImgui(m_Engine);
}

void PlaneEngine::update()
{
    if (m_NormalHotReload)
    {
        if (m_PushConstants.patchSize != m_Noise.normalPushConstants.patchSize)
        {
            m_Noise.normalPushConstants.patchSize = m_PushConstants.patchSize;
            m_Engine.setPlaneNormalDirty();
        }
        if (m_PushConstants.gridSize != m_Noise.normalPushConstants.gridSize)
        {
            m_Noise.normalPushConstants.gridSize = m_PushConstants.gridSize;
            m_Engine.setPlaneNormalDirty();
        }
        if (m_PushConstants.heightScale != m_Noise.normalPushConstants.heightScale)
        {
            m_Noise.normalPushConstants.heightScale = m_PushConstants.heightScale;
            m_Engine.setPlaneNormalDirty();
        }
    }

    m_PushConstants.mvp = m_Engine.getCamera().getVPMatrix();
    m_PushConstants.cameraPos = m_Engine.getCamera().getPosition();

    glm::vec2 l_CameraTile = glm::vec2(m_PushConstants.cameraPos.x, m_PushConstants.cameraPos.z);
    l_CameraTile = glm::round(l_CameraTile / m_PushConstants.patchSize) * m_PushConstants.patchSize;
    if (l_CameraTile != m_PushConstants.cameraTile)
    {
        if (m_NoiseHotReload)
            m_Engine.setPlaneNoiseDirty();
        m_PushConstants.cameraTile = l_CameraTile;
    }

    if (m_Engine.isNoiseDirty() && m_NormalHotReload)
    {
        m_Engine.setPlaneNormalDirty();
    }

    const float l_WorldExtent = m_PushConstants.gridSize * m_PushConstants.patchSize;
    m_Noise.noisePushConstants.offset = m_NoiseOffset + (m_PushConstants.cameraTile / l_WorldExtent);
}

void PlaneEngine::render(const VulkanCommandBuffer& p_CmdBuffer) const
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

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, m_Wireframe ? m_TessellationPipelineWFID : m_TessellationPipelineID);
    p_CmdBuffer.cmdSetViewport(viewport);
    p_CmdBuffer.cmdSetScissor(scissor);
    
    p_CmdBuffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, m_TessellationPipelineLayoutID, m_TessellationDescriptorSetID);
    p_CmdBuffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_VERTEX_BIT, PushConstantData::getVertexShaderOffset(), PushConstantData::getVertexShaderSize(), m_PushConstants.getVertexShaderData());
    p_CmdBuffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, PushConstantData::getTessellationControlShaderOffset(), PushConstantData::getTessellationControlShaderSize(), m_PushConstants.getTessellationControlShaderData());
    p_CmdBuffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, PushConstantData::getTessellationEvaluationShaderOffset(), PushConstantData::getTessellationEvaluationShaderSize(), m_PushConstants.getTessellationEvaluationShaderData());
    p_CmdBuffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_FRAGMENT_BIT, PushConstantData::getFragmentShaderOffset(), PushConstantData::getFragmentShaderSize(), m_PushConstants.getFragmentShaderData());
    
    p_CmdBuffer.cmdDraw(m_PushConstants.gridSize * m_PushConstants.gridSize * 4, 0);
}

void PlaneEngine::cleanup()
{
    m_Noise.cleanup();
}

void PlaneEngine::drawImgui()
{
    ImGui::Begin("Controls");

    ImGui::DragFloat("Height scale", &m_PushConstants.heightScale, 0.1f);
    ImGui::Separator();
    int l_GridSize = m_PushConstants.gridSize;
    ImGui::DragInt("Grid size", &l_GridSize, 1, 1, 100);
    m_PushConstants.gridSize = static_cast<uint32_t>(l_GridSize);
    ImGui::DragFloat("Patch size", &m_PushConstants.patchSize, 0.1f, 1.f, 100.f);
    ImGui::Separator();
    ImGui::DragFloat("Tessellation min", &m_PushConstants.minTessLevel, 0.1f, 1.f, 64.f);
    if (m_PushConstants.minTessLevel > m_PushConstants.maxTessLevel)
        m_PushConstants.minTessLevel = m_PushConstants.maxTessLevel;
    ImGui::DragFloat("Tessellation max", &m_PushConstants.maxTessLevel, 0.1f, 1.f, 64.f);
    if (m_PushConstants.minTessLevel > m_PushConstants.maxTessLevel)
        m_PushConstants.maxTessLevel = m_PushConstants.minTessLevel;
    ImGui::DragFloat("Tessellation factor", &m_PushConstants.tessFactor, 0.001f, 0.01f, 1.f);
    ImGui::DragFloat("Tessellation slope", &m_PushConstants.tessSlope, 0.01f, 0.01f, 2.f);
    ImGui::Separator();
    ImGui::ColorEdit3("Color", &m_PushConstants.color.x);
    ImGui::Separator();
    ImGui::Checkbox("Wireframe", &m_Wireframe);
    ImGui::Separator();
    ImGui::Checkbox("Noise Hot Reload", &m_NoiseHotReload);
    if (!m_NoiseHotReload)
    {
        ImGui::DragFloat2("Noise offset", &m_Noise.noisePushConstants.offset.x, 0.01f);
        ImGui::DragFloat("Noise scale", &m_Noise.noisePushConstants.scale, 0.01f);
        ImGui::DragFloat("Noise W", &m_Noise.noisePushConstants.w, 0.01f);
        if (ImGui::Button("Recompute Noise"))
        {
            m_Engine.setPlaneNoiseDirty();
        }
    }
    else
    {
        glm::vec2 l_Offset = m_NoiseOffset;
        ImGui::DragFloat2("Noise offset", &l_Offset.x, 0.01f);
        if (l_Offset != m_NoiseOffset)
        {
            m_NoiseOffset = l_Offset;
            m_Engine.setPlaneNoiseDirty();
        }
        float l_Scale = m_Noise.noisePushConstants.scale;
        ImGui::DragFloat("Noise scale", &l_Scale, 1.f, 1.f, 100.f);
        if (l_Scale != m_Noise.noisePushConstants.scale)
        {
            m_Noise.noisePushConstants.scale = l_Scale;
            m_Engine.setPlaneNoiseDirty();
        }
        float l_W = m_W;
        ImGui::DragFloat("Noise W", &l_W, 0.01f);
        if (l_W != m_W)
        {
            m_W = l_W;
            m_Engine.setPlaneNoiseDirty();
        }
        ImGui::Checkbox("Animated W", &m_WAnimated);
        if (m_WAnimated)
        {
            ImGui::DragFloat("W speed", &m_WSpeed, 0.01f);

            m_WOffset += m_WSpeed * ImGui::GetIO().DeltaTime;
            m_Noise.noisePushConstants.w = m_W + m_WOffset;
            m_Engine.setPlaneNoiseDirty();
        }
    }
    ImGui::Separator();
    ImGui::Checkbox("Normal Hot Reload", &m_NormalHotReload);
    if (!m_NormalHotReload)
    {
        ImGui::DragFloat("Normal offset", &m_Noise.normalPushConstants.offsetScale, 0.001f, 0.001f, 0.1f);
        if (ImGui::Button("Recompute Normal"))
        {
            m_Engine.setPlaneNormalDirty();
        }
    }
    else
    {
        float l_OffsetScale = m_Noise.normalPushConstants.offsetScale;
        ImGui::DragFloat("Normal offset", &l_OffsetScale, 0.001f, 0.01f, 0.1f);
        if (l_OffsetScale != m_Noise.normalPushConstants.offsetScale)
        {
            m_Noise.normalPushConstants.offsetScale = l_OffsetScale;
            m_Engine.setPlaneNormalDirty();
        }
    }
    ImGui::Separator();
    ImGui::BeginDisabled(m_Engine.getNoiseEngine().getImguiObject() == &m_Noise);
    if (ImGui::Button("Preview Images"))
    {
        m_Engine.getNoiseEngine().setImguiObject(&m_Noise);
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void PlaneEngine::updateNoise(const VulkanCommandBuffer& p_CmdBuffer) const
{
    m_Engine.getNoiseEngine().updateNoise(p_CmdBuffer, m_Noise);
}

void PlaneEngine::updateNormal(const VulkanCommandBuffer& p_CmdBuffer) const
{
    m_Engine.getNoiseEngine().updateNormal(p_CmdBuffer, m_Noise);
}

void PlaneEngine::createPipelines()
{
    VulkanDevice& l_Device = m_Engine.getDevice();

    std::array<VkPushConstantRange, 4> l_PushConstantRanges;
    l_PushConstantRanges[0] = { VK_SHADER_STAGE_VERTEX_BIT, PushConstantData::getVertexShaderOffset(), PushConstantData::getVertexShaderSize() };
    l_PushConstantRanges[1] = { VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, PushConstantData::getTessellationControlShaderOffset(), PushConstantData::getTessellationControlShaderSize() };
    l_PushConstantRanges[2] = { VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, PushConstantData::getTessellationEvaluationShaderOffset(), PushConstantData::getTessellationEvaluationShaderSize() };
    l_PushConstantRanges[3] = { VK_SHADER_STAGE_FRAGMENT_BIT, PushConstantData::getFragmentShaderOffset(), PushConstantData::getFragmentShaderSize() };
    std::array<ResourceID, 1> l_DescriptorSetLayouts = { m_TessellationDescriptorSetLayoutID };
    m_TessellationPipelineLayoutID = l_Device.createPipelineLayout(l_DescriptorSetLayouts, l_PushConstantRanges);

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

    const uint32_t vertexShaderID = l_Device.createShader("shaders/plane.vert", VK_SHADER_STAGE_VERTEX_BIT, false, {});
    const uint32_t fragmentShaderID = l_Device.createShader("shaders/plane.frag", VK_SHADER_STAGE_FRAGMENT_BIT, false, {});
    const uint32_t tessellationControlShaderID = l_Device.createShader("shaders/plane.tesc", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, false, {});
    const uint32_t tessellationEvaluationShaderID = l_Device.createShader("shaders/plane.tese", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, false, {});

    VulkanPipelineBuilder l_TessellationBuilder{l_Device.getID()};
    l_TessellationBuilder.setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST, VK_FALSE);
    l_TessellationBuilder.setTessellationState(4);
    l_TessellationBuilder.setViewportState(1, 1);
    l_TessellationBuilder.setRasterizationState(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    l_TessellationBuilder.setMultisampleState(VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 1.0f);
    l_TessellationBuilder.setDepthStencilState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);
    l_TessellationBuilder.addColorBlendAttachment(l_ColorBlendAttachment);
    l_TessellationBuilder.setColorBlendState(VK_FALSE, VK_LOGIC_OP_COPY, { 0.0f, 0.0f, 0.0f, 0.0f });
    l_TessellationBuilder.setDynamicState(l_DynamicStates);
    l_TessellationBuilder.addShaderStage(vertexShaderID, "main");
    l_TessellationBuilder.addShaderStage(fragmentShaderID, "main");
    l_TessellationBuilder.addShaderStage(tessellationControlShaderID, "main");
    l_TessellationBuilder.addShaderStage(tessellationEvaluationShaderID, "main");

    m_TessellationPipelineID = l_Device.createPipeline(l_TessellationBuilder, m_TessellationPipelineLayoutID, m_Engine.getRenderPassID(), 0);

    l_TessellationBuilder.setRasterizationState(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    m_TessellationPipelineWFID = l_Device.createPipeline(l_TessellationBuilder, m_TessellationPipelineLayoutID, m_Engine.getRenderPassID(), 0);

    l_Device.freeShader(vertexShaderID);
    l_Device.freeShader(fragmentShaderID);
    l_Device.freeShader(tessellationControlShaderID);
    l_Device.freeShader(tessellationEvaluationShaderID);
}

void PlaneEngine::createHeightmapDescriptorSets(const uint32_t p_TextureSize)
{
    VulkanDevice& l_Device = m_Engine.getDevice();

    m_Noise.initialize(p_TextureSize, m_Engine, true);

    {
        std::array<VkDescriptorSetLayoutBinding, 2> l_Bindings;
        l_Bindings[0].binding = 0;
        l_Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_Bindings[0].descriptorCount = 1;
        l_Bindings[0].stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        l_Bindings[0].pImmutableSamplers = nullptr;
        l_Bindings[1].binding = 1;
        l_Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_Bindings[1].descriptorCount = 1;
        l_Bindings[1].stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        l_Bindings[1].pImmutableSamplers = nullptr;

        m_TessellationDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
    }

    m_TessellationDescriptorSetID = l_Device.createDescriptorSet(m_Engine.getDescriptorPoolID(), m_TessellationDescriptorSetLayoutID);

    VulkanImage& l_HeightmapImage = l_Device.getImage(m_Noise.noiseImage.image);
    VulkanImage& l_NormalmapImage = l_Device.getImage(m_Noise.normalImage.image);

    std::array<VkWriteDescriptorSet, 1> l_Write{};
    std::array<VkDescriptorImageInfo, 2> l_ImageInfos;
    {
        l_ImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_ImageInfos[0].imageView = *l_HeightmapImage.getImageView(m_Noise.noiseImage.view);
        l_ImageInfos[0].sampler = *l_HeightmapImage.getSampler(m_Noise.noiseImage.sampler);
        l_ImageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_ImageInfos[1].imageView = *l_NormalmapImage.getImageView(m_Noise.normalImage.view);
        l_ImageInfos[1].sampler = *l_NormalmapImage.getSampler(m_Noise.normalImage.sampler);

        l_Write[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_Write[0].dstSet = *l_Device.getDescriptorSet(m_TessellationDescriptorSetID);
        l_Write[0].dstBinding = 0;
        l_Write[0].dstArrayElement = 0;
        l_Write[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_Write[0].descriptorCount = 2;
        l_Write[0].pImageInfo = l_ImageInfos.data();
    }

    l_Device.updateDescriptorSets(l_Write);
}
