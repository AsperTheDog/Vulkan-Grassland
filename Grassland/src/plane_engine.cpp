#include "plane_engine.hpp"

#include "engine.hpp"
#include "imgui.h"
#include "vulkan_device.hpp"
#include "backends/imgui_impl_vulkan.h"
#include "ext/vulkan_swapchain.hpp"
#include "utils/logger.hpp"

void PlaneEngine::initialize(const uint32_t p_ImgSize)
{
    createHeightmapDescriptorSets(p_ImgSize, p_ImgSize);
    createPipelines();
}

void PlaneEngine::initializeImgui()
{
    VulkanDevice& l_Device = m_Engine.getDevice();

    VulkanImage l_HeightmapImage = l_Device.getImage(m_HeightmapID);
    m_HeightmapDescriptorSet = ImGui_ImplVulkan_AddTexture(*l_HeightmapImage.getSampler(m_HeightmapSamplerID), *l_HeightmapImage.getImageView(m_HeightmapViewID), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VulkanImage l_NormalmapImage = l_Device.getImage(m_NormalmapID);
    m_NormalmapDescriptorSet = ImGui_ImplVulkan_AddTexture(*l_NormalmapImage.getSampler(m_NormalmapSamplerID), *l_NormalmapImage.getImageView(m_NormalmapViewID), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

}

void PlaneEngine::update()
{
    if (m_NormalHotReload)
    {
        if (m_PushConstants.patchSize != m_NormalPushConstants.patchSize)
        {
            m_NormalPushConstants.patchSize = m_PushConstants.patchSize;
            m_Engine.setNormalDirty();
        }
        if (m_PushConstants.gridSize != m_NormalPushConstants.gridSize)
        {
            m_NormalPushConstants.gridSize = m_PushConstants.gridSize;
            m_Engine.setNormalDirty();
        }
        if (m_PushConstants.heightScale != m_NormalPushConstants.heightScale)
        {
            m_NormalPushConstants.heightScale = m_PushConstants.heightScale;
            m_Engine.setNormalDirty();
        }
    }

    m_PushConstants.mvp = m_Engine.getCamera().getVPMatrix();
    m_PushConstants.cameraPos = m_Engine.getCamera().getPosition();

    glm::vec2 l_CameraTile = glm::vec2(m_PushConstants.cameraPos.x, m_PushConstants.cameraPos.z);
    l_CameraTile = glm::round(l_CameraTile / m_PushConstants.patchSize) * m_PushConstants.patchSize;
    if (l_CameraTile != m_PushConstants.cameraTile)
    {
        if (m_NoiseHotReload)
            m_Engine.setNoiseDirty();
        m_PushConstants.cameraTile = l_CameraTile;
    }

    if (m_Engine.isNoiseDirty() && m_NormalHotReload)
    {
        m_Engine.setNormalDirty();
    }

    const float l_WorldExtent = m_PushConstants.gridSize * m_PushConstants.patchSize;
    m_NoisePushConstants.offset = m_NoiseOffset + (m_PushConstants.cameraTile / l_WorldExtent);
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
    p_CmdBuffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_VERTEX_BIT, PushConstantData::getVertexShaderOffset(), PushConstantData::getVertexShaderSize(), &m_PushConstants);
    p_CmdBuffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, PushConstantData::getTessellationControlShaderOffset(), PushConstantData::getTessellationControlShaderSize(), &m_PushConstants.cameraPos);
    p_CmdBuffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, PushConstantData::getTessellationEvaluationShaderOffset(), PushConstantData::getTessellationEvaluationShaderSize(), &m_PushConstants.heightScale);
    p_CmdBuffer.cmdPushConstant(m_TessellationPipelineLayoutID, VK_SHADER_STAGE_FRAGMENT_BIT, PushConstantData::getFragmentShaderOffset(), PushConstantData::getFragmentShaderSize(), &m_PushConstants.color);
    
    p_CmdBuffer.cmdDraw(m_PushConstants.gridSize * m_PushConstants.gridSize * 4, 0);
}

void PlaneEngine::cleanup()
{
    ImGui_ImplVulkan_RemoveTexture(m_HeightmapDescriptorSet);
    ImGui_ImplVulkan_RemoveTexture(m_NormalmapDescriptorSet);
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
        ImGui::DragFloat2("Noise offset", &m_NoisePushConstants.offset.x, 0.01f);
        ImGui::DragFloat("Noise scale", &m_NoisePushConstants.scale, 0.01f);
        ImGui::DragFloat("Noise W", &m_NoisePushConstants.w, 0.01f);
        if (ImGui::Button("Recompute Noise"))
        {
            m_Engine.setNoiseDirty();
        }
    }
    else
    {
        glm::vec2 l_Offset = m_NoiseOffset;
        ImGui::DragFloat2("Noise offset", &l_Offset.x, 0.01f);
        if (l_Offset != m_NoiseOffset)
        {
            m_NoiseOffset = l_Offset;
            m_Engine.setNoiseDirty();
        }
        float l_Scale = m_NoisePushConstants.scale;
        ImGui::DragFloat("Noise scale", &l_Scale, 1.f, 1.f, 100.f);
        if (l_Scale != m_NoisePushConstants.scale)
        {
            m_NoisePushConstants.scale = l_Scale;
            m_Engine.setNoiseDirty();
        }
        float l_W = m_W;
        ImGui::DragFloat("Noise W", &l_W, 0.01f);
        if (l_W != m_W)
        {
            m_W = l_W;
            m_Engine.setNoiseDirty();
        }
        ImGui::Checkbox("Animated W", &m_WAnimated);
        if (m_WAnimated)
        {
            ImGui::DragFloat("W speed", &m_WSpeed, 0.01f);

            m_WOffset += m_WSpeed * ImGui::GetIO().DeltaTime;
            m_NoisePushConstants.w = m_W + m_WOffset;
            m_Engine.setNoiseDirty();
        }
    }
    ImGui::Separator();
    ImGui::Checkbox("Normal Hot Reload", &m_NormalHotReload);
    if (!m_NormalHotReload)
    {
        ImGui::DragFloat("Normal offset", &m_NormalPushConstants.offsetScale, 0.001f, 0.001f, 0.1f);
        if (ImGui::Button("Recompute Normal"))
        {
            m_Engine.setNormalDirty();
        }
    }
    else
    {
        float l_OffsetScale = m_NormalPushConstants.offsetScale;
        ImGui::DragFloat("Normal offset", &l_OffsetScale, 0.001f, 0.01f, 0.1f);
        if (l_OffsetScale != m_NormalPushConstants.offsetScale)
        {
            m_NormalPushConstants.offsetScale = l_OffsetScale;
            m_Engine.setNormalDirty();
        }
    }
    ImGui::Separator();
    ImGui::BeginDisabled(m_ShowImagePanel);
    if (ImGui::Button("Preview Images"))
    {
        m_ShowImagePanel = true;
        m_ImagePreview = NONE;
    }
    ImGui::EndDisabled();
    ImGui::End();

    if (m_ShowImagePanel)
    {
        ImGui::Begin("Images", &m_ShowImagePanel);
        // ComboBox
        const char* l_ImageNames[] = { "None", "Heightmap", "Normalmap" };
        if (ImGui::BeginCombo("Image", l_ImageNames[m_ImagePreview]))
        {
            for (int i = 0; i < 3; i++)
            {
                const bool l_Selected = (m_ImagePreview == i);
                if (ImGui::Selectable(l_ImageNames[i], l_Selected))
                {
                    m_ImagePreview = static_cast<ImagePreview>(i);
                }
                if (l_Selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::End();

        switch (m_ImagePreview)
        {
        case NONE:
            break;
        case HEIGHTMAP:
            if (m_HeightmapDescriptorSet == VK_NULL_HANDLE)
                break;
            {
                ImGui::Begin("Texture");
                ImGui::Image(reinterpret_cast<ImTextureID>(m_HeightmapDescriptorSet), ImVec2(256, 256));
                ImGui::End();
            }
            break;
        case NORMALMAP:
            if (m_NormalmapDescriptorSet == VK_NULL_HANDLE)
                break;
            {
                ImGui::Begin("Texture");
                ImGui::Image(reinterpret_cast<ImTextureID>(m_NormalmapDescriptorSet), ImVec2(256, 256));
                ImGui::End();
            }
            break;
        }
    }
}

void PlaneEngine::updateNoise(const VulkanCommandBuffer& p_CmdBuffer) const
{
    VulkanDevice& l_Device = m_Engine.getDevice();
    
    VulkanImage& l_HeightmapImage = l_Device.getImage(m_HeightmapID);
    const VkExtent3D l_ImageSize = l_HeightmapImage.getSize();
    const uint32_t groupCountX = (l_ImageSize.width + 7) / 8;
    const uint32_t groupCountY = (l_ImageSize.height + 7) / 8;

    const uint32_t l_ComputeFamilyIndex = m_Engine.getComputeQueuePos().familyIndex;

    VulkanMemoryBarrierBuilder l_EnterBarrierBuilder{l_Device.getID(), VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
    l_EnterBarrierBuilder.addImageMemoryBarrier(m_HeightmapID, VK_IMAGE_LAYOUT_GENERAL, l_ComputeFamilyIndex, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    p_CmdBuffer.cmdPipelineBarrier(l_EnterBarrierBuilder);
    l_HeightmapImage.setLayout(VK_IMAGE_LAYOUT_GENERAL);
    l_HeightmapImage.setQueue(l_ComputeFamilyIndex);

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeNoisePipelineID);
    p_CmdBuffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeNoisePipelineLayoutID, m_ComputeNoiseDescriptorSetID);
    p_CmdBuffer.cmdPushConstant(m_ComputeNoisePipelineLayoutID, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NoisePushConstantData), &m_NoisePushConstants);
    p_CmdBuffer.cmdDispatch(groupCountX, groupCountY, 1);

    VulkanMemoryBarrierBuilder l_ExitBarrierBuilder{l_Device.getID(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
    l_ExitBarrierBuilder.addImageMemoryBarrier(m_HeightmapID, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, l_ComputeFamilyIndex, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    p_CmdBuffer.cmdPipelineBarrier(l_ExitBarrierBuilder);
    l_HeightmapImage.setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void PlaneEngine::updateNormal(const VulkanCommandBuffer& p_CmdBuffer) const
{
    VulkanDevice& l_Device = m_Engine.getDevice();

    VulkanImage& l_NormalmapImage = l_Device.getImage(m_NormalmapID);
    const VkExtent3D l_ImageSize = l_NormalmapImage.getSize();
    const uint32_t groupCountX = (l_ImageSize.width + 7) / 8;
    const uint32_t groupCountY = (l_ImageSize.height + 7) / 8;

    const uint32_t l_ComputeFamilyIndex = m_Engine.getComputeQueuePos().familyIndex;
    const uint32_t l_GraphicsFamilyIndex = m_Engine.getGraphicsQueuePos().familyIndex;

    VulkanMemoryBarrierBuilder l_EnterBarrierBuilder{l_Device.getID(), VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
    l_EnterBarrierBuilder.addImageMemoryBarrier(m_NormalmapID, VK_IMAGE_LAYOUT_GENERAL, l_ComputeFamilyIndex, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    p_CmdBuffer.cmdPipelineBarrier(l_EnterBarrierBuilder);
    l_NormalmapImage.setLayout(VK_IMAGE_LAYOUT_GENERAL);
    l_NormalmapImage.setQueue(l_ComputeFamilyIndex);

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeNormalPipelineID);
    p_CmdBuffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeNormalPipelineLayoutID, m_ComputeNormalDescriptorSetID);
    p_CmdBuffer.cmdPushConstant(m_ComputeNormalPipelineLayoutID, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NormalPushConstantData), &m_NormalPushConstants);
    p_CmdBuffer.cmdDispatch(groupCountX, groupCountY, 1);

    VulkanMemoryBarrierBuilder l_ExitBarrierBuilder{l_Device.getID(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, 0};
    l_ExitBarrierBuilder.addImageMemoryBarrier(m_NormalmapID, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, l_GraphicsFamilyIndex, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    p_CmdBuffer.cmdPipelineBarrier(l_ExitBarrierBuilder);
    l_NormalmapImage.setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    l_NormalmapImage.setQueue(l_GraphicsFamilyIndex);
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

    std::array<VkPushConstantRange, 1> l_ComputeNoisePushConstantRanges;
    l_ComputeNoisePushConstantRanges[0] = { VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NoisePushConstantData)} };
    std::array<ResourceID, 1> l_ComputeDescriptorSetLayouts = { m_ComputeNoiseDescriptorSetLayoutID };
    m_ComputeNoisePipelineLayoutID = l_Device.createPipelineLayout(l_ComputeDescriptorSetLayouts, l_ComputeNoisePushConstantRanges);

    const uint32_t l_ComputeShaderID = l_Device.createShader("shaders/noise.comp", VK_SHADER_STAGE_COMPUTE_BIT, false, {});
    m_ComputeNoisePipelineID = l_Device.createComputePipeline(m_ComputeNoisePipelineLayoutID, l_ComputeShaderID, "main");

    l_Device.freeShader(l_ComputeShaderID);

    std::array<VkPushConstantRange, 1> l_ComputeNormalPushConstantRanges;
    l_ComputeNormalPushConstantRanges[0] = { VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NormalPushConstantData)} };
    std::array<ResourceID, 1> l_ComputeNormalDescriptorSetLayouts = { m_ComputeNormalDescriptorSetLayoutID };
    m_ComputeNormalPipelineLayoutID = l_Device.createPipelineLayout(l_ComputeNormalDescriptorSetLayouts, l_ComputeNormalPushConstantRanges);

    const uint32_t l_ComputeNormalShaderID = l_Device.createShader("shaders/normal.comp", VK_SHADER_STAGE_COMPUTE_BIT, false, {});
    m_ComputeNormalPipelineID = l_Device.createComputePipeline(m_ComputeNormalPipelineLayoutID, l_ComputeNormalShaderID, "main");
}

void PlaneEngine::createHeightmapDescriptorSets(const uint32_t p_TextWidth, const uint32_t p_TextHeight)
{
    VulkanDevice& l_Device = m_Engine.getDevice();

    const VkExtent3D extent = { p_TextWidth, p_TextHeight, 1 };
    const uint32_t l_ComputeFamilyIndex = m_Engine.getComputeQueuePos().familyIndex;

    m_NoisePushConstants.size = { p_TextWidth, p_TextHeight };

    m_HeightmapID = l_Device.createImage(VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 0);
    VulkanImage& l_HeightmapImage = l_Device.getImage(m_HeightmapID);
    l_HeightmapImage.allocateFromFlags({ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false });
    l_HeightmapImage.setQueue(l_ComputeFamilyIndex);

    m_HeightmapViewID = l_HeightmapImage.createImageView(VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
    m_HeightmapSamplerID = l_HeightmapImage.createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    m_NormalmapID = l_Device.createImage(VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 0);
    VulkanImage& l_NormalmapImage = l_Device.getImage(m_NormalmapID);
    l_NormalmapImage.allocateFromFlags({ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false });
    l_NormalmapImage.setQueue(l_ComputeFamilyIndex);

    m_NormalmapViewID = l_NormalmapImage.createImageView(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
    m_NormalmapSamplerID = l_NormalmapImage.createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

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

    {
        std::array<VkDescriptorSetLayoutBinding, 2> l_Bindings;
        l_Bindings[0].binding = 0;
        l_Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_Bindings[0].descriptorCount = 1;
        l_Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        l_Bindings[0].pImmutableSamplers = nullptr;
        l_Bindings[1].binding = 1;
        l_Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        l_Bindings[1].descriptorCount = 1;
        l_Bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        l_Bindings[1].pImmutableSamplers = nullptr;

        m_ComputeNormalDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
    }

    m_ComputeNormalDescriptorSetID = l_Device.createDescriptorSet(m_Engine.getDescriptorPoolID(), m_ComputeNormalDescriptorSetLayoutID);

    {
        std::array<VkDescriptorSetLayoutBinding, 1> l_Bindings;
        l_Bindings[0].binding = 0;
        l_Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        l_Bindings[0].descriptorCount = 1;
        l_Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        l_Bindings[0].pImmutableSamplers = nullptr;

        m_ComputeNoiseDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
    }

    m_ComputeNoiseDescriptorSetID = l_Device.createDescriptorSet(m_Engine.getDescriptorPoolID(), m_ComputeNoiseDescriptorSetLayoutID);

    std::array<VkWriteDescriptorSet, 4> l_Writes{};

    std::array<VkDescriptorImageInfo, 2> l_ImageInfos;
    {
        l_ImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_ImageInfos[0].imageView = *l_HeightmapImage.getImageView(m_HeightmapViewID);
        l_ImageInfos[0].sampler = *l_HeightmapImage.getSampler(m_HeightmapSamplerID);
        l_ImageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_ImageInfos[1].imageView = *l_NormalmapImage.getImageView(m_NormalmapViewID);
        l_ImageInfos[1].sampler = *l_NormalmapImage.getSampler(m_NormalmapSamplerID);

        l_Writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_Writes[0].dstSet = *l_Device.getDescriptorSet(m_TessellationDescriptorSetID);
        l_Writes[0].dstBinding = 0;
        l_Writes[0].dstArrayElement = 0;
        l_Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_Writes[0].descriptorCount = 2;
        l_Writes[0].pImageInfo = l_ImageInfos.data();
    }

    VkDescriptorImageInfo l_ComputeImageInfo;
    {
        l_ComputeImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        l_ComputeImageInfo.imageView = *l_HeightmapImage.getImageView(m_HeightmapViewID);
        l_ComputeImageInfo.sampler = *l_HeightmapImage.getSampler(m_HeightmapSamplerID);

        l_Writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_Writes[1].dstSet = *l_Device.getDescriptorSet(m_ComputeNoiseDescriptorSetID);
        l_Writes[1].dstBinding = 0;
        l_Writes[1].dstArrayElement = 0;
        l_Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        l_Writes[1].descriptorCount = 1;
        l_Writes[1].pImageInfo = &l_ComputeImageInfo;
    }

    std::array<VkDescriptorImageInfo, 2> l_ComputeNormalImageInfos;
    {
        l_ComputeNormalImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_ComputeNormalImageInfos[0].imageView = *l_HeightmapImage.getImageView(m_HeightmapViewID);
        l_ComputeNormalImageInfos[0].sampler = *l_HeightmapImage.getSampler(m_HeightmapSamplerID);

        l_ComputeNormalImageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        l_ComputeNormalImageInfos[1].imageView = *l_NormalmapImage.getImageView(m_NormalmapViewID);
        l_ComputeNormalImageInfos[1].sampler = *l_NormalmapImage.getSampler(m_NormalmapSamplerID);

        l_Writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_Writes[2].dstSet = *l_Device.getDescriptorSet(m_ComputeNormalDescriptorSetID);
        l_Writes[2].dstBinding = 0;
        l_Writes[2].dstArrayElement = 0;
        l_Writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_Writes[2].descriptorCount = 1;
        l_Writes[2].pImageInfo = &l_ComputeNormalImageInfos[0];

        l_Writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_Writes[3].dstSet = *l_Device.getDescriptorSet(m_ComputeNormalDescriptorSetID);
        l_Writes[3].dstBinding = 1;
        l_Writes[3].dstArrayElement = 0;
        l_Writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        l_Writes[3].descriptorCount = 1;
        l_Writes[3].pImageInfo = &l_ComputeNormalImageInfos[1];
    }

    l_Device.updateDescriptorSets(l_Writes);
}
