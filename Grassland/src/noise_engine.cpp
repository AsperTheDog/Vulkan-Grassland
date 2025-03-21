#include "noise_engine.hpp"

#include "engine.hpp"
#include "vulkan_device.hpp"
#include "backends/imgui_impl_vulkan.h"
#include "utils/logger.hpp"

void NoiseEngine::NoiseObject::initialize(uint32_t p_Size, Engine& p_Engine, const bool p_IncludeNormal)
{
    includeNormal = p_IncludeNormal;

    m_NoiseEngine = &p_Engine.getNoiseEngine();

    const Engine& l_Engine = m_NoiseEngine->getEngine();
    VulkanDevice& l_Device = l_Engine.getDevice();

    const VkExtent3D extent = { p_Size, p_Size, 1 };
    const uint32_t l_ComputeFamilyIndex = l_Engine.getComputeQueuePos().familyIndex;

    noisePushConstants.size = { p_Size, p_Size };

    noiseImage.image = l_Device.createImage(VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 0);
    VulkanImage& l_HeightmapImage = l_Device.getImage(noiseImage.image);
    l_HeightmapImage.allocateFromFlags({ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false });
    l_HeightmapImage.setQueue(l_ComputeFamilyIndex);

    noiseImage.view = l_HeightmapImage.createImageView(VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
    noiseImage.sampler = l_HeightmapImage.createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    computeNoiseDescriptorSetID = l_Device.createDescriptorSet(l_Engine.getDescriptorPoolID(), m_NoiseEngine->m_ComputeNoiseDescriptorSetLayoutID);

    if (includeNormal)
    {
        normalImage.image = l_Device.createImage(VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 0);
        VulkanImage& l_NormalmapImage = l_Device.getImage(normalImage.image);
        l_NormalmapImage.allocateFromFlags({ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false });
        l_NormalmapImage.setQueue(l_ComputeFamilyIndex);

        normalImage.view = l_NormalmapImage.createImageView(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
        normalImage.sampler = l_NormalmapImage.createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        computeNormalDescriptorSetID = l_Device.createDescriptorSet(l_Engine.getDescriptorPoolID(), m_NoiseEngine->m_ComputeNormalDescriptorSetLayoutID);
    }

    {
        std::array<VkWriteDescriptorSet, 1> l_Writes{};
        VkDescriptorImageInfo l_ComputeImageInfo;
        {
            l_ComputeImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            l_ComputeImageInfo.imageView = *l_HeightmapImage.getImageView(noiseImage.view);
            l_ComputeImageInfo.sampler = *l_HeightmapImage.getSampler(noiseImage.sampler);

            l_Writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_Writes[0].dstSet = *l_Device.getDescriptorSet(computeNoiseDescriptorSetID);
            l_Writes[0].dstBinding = 0;
            l_Writes[0].dstArrayElement = 0;
            l_Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            l_Writes[0].descriptorCount = 1;
            l_Writes[0].pImageInfo = &l_ComputeImageInfo;
        }

        l_Device.updateDescriptorSets(l_Writes);
    }

    if (includeNormal)
    {
        VulkanImage& l_NormalmapImage = l_Device.getImage(normalImage.image);

        std::array<VkWriteDescriptorSet, 2> l_Writes{};
        std::array<VkDescriptorImageInfo, 2> l_ComputeNormalImageInfos;
        {
            l_ComputeNormalImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_ComputeNormalImageInfos[0].imageView = *l_HeightmapImage.getImageView(noiseImage.view);
            l_ComputeNormalImageInfos[0].sampler = *l_HeightmapImage.getSampler(noiseImage.sampler);

            l_ComputeNormalImageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            l_ComputeNormalImageInfos[1].imageView = *l_NormalmapImage.getImageView(normalImage.view);
            l_ComputeNormalImageInfos[1].sampler = *l_NormalmapImage.getSampler(normalImage.sampler);

            l_Writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_Writes[0].dstSet = *l_Device.getDescriptorSet(computeNormalDescriptorSetID);
            l_Writes[0].dstBinding = 0;
            l_Writes[0].dstArrayElement = 0;
            l_Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_Writes[0].descriptorCount = 1;
            l_Writes[0].pImageInfo = &l_ComputeNormalImageInfos[0];

            l_Writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_Writes[1].dstSet = *l_Device.getDescriptorSet(computeNormalDescriptorSetID);
            l_Writes[1].dstBinding = 1;
            l_Writes[1].dstArrayElement = 0;
            l_Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            l_Writes[1].descriptorCount = 1;
            l_Writes[1].pImageInfo = &l_ComputeNormalImageInfos[1];
        }

        l_Device.updateDescriptorSets(l_Writes);
    }
}

void NoiseEngine::NoiseObject::initializeImgui()
{
    VulkanDevice& l_Device = m_NoiseEngine->getEngine().getDevice();

    VulkanImage l_HeightmapImage = l_Device.getImage(noiseImage.image);
    imguiHeightmapDescriptorSet = ImGui_ImplVulkan_AddTexture(*l_HeightmapImage.getSampler(noiseImage.sampler), *l_HeightmapImage.getImageView(noiseImage.view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (includeNormal)
    {
        VulkanImage l_NormalmapImage = l_Device.getImage(normalImage.image);
        imguiNormalmapDescriptorSet = ImGui_ImplVulkan_AddTexture(*l_NormalmapImage.getSampler(normalImage.sampler), *l_NormalmapImage.getImageView(normalImage.view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void NoiseEngine::NoiseObject::updatePatchSize(const float p_PatchSize)
{
    if (includeNormal)
    {
        if (normalHotReload && p_PatchSize != normalPushConstants.patchSize)
            normalNeedsRebuild = true;
        normalPushConstants.patchSize = p_PatchSize;
    }
}

void NoiseEngine::NoiseObject::updateGridSize(const uint32_t p_GridSize)
{
    if (includeNormal)
    {
        if (normalHotReload && p_GridSize != normalPushConstants.gridSize)
            normalNeedsRebuild = true;
        normalPushConstants.gridSize = p_GridSize;
    }
}

void NoiseEngine::NoiseObject::updateHeightScale(const float p_HeightScale)
{
    if (includeNormal)
    {
        if (normalHotReload && p_HeightScale != normalPushConstants.heightScale)
            normalNeedsRebuild = true;
        normalPushConstants.heightScale = p_HeightScale;
    }
}

void NoiseEngine::NoiseObject::updateOffset(const glm::vec2 p_Offset)
{
    if (noiseHotReload && p_Offset != noisePushConstants.offset)
    {
        noiseNeedsRebuild = true;
        if (includeNormal)
            normalNeedsRebuild = true;
    }
    noisePushConstants.offset = m_NoiseOffset + p_Offset;
}

void NoiseEngine::NoiseObject::shiftOffset(const glm::vec2 p_Offset)
{
    if (noiseHotReload && p_Offset != glm::vec2{0.f})
    {
        noiseNeedsRebuild = true;
        if (includeNormal)
            normalNeedsRebuild = true;
    }
    noisePushConstants.offset += m_NoiseOffset + p_Offset;
}

void NoiseEngine::NoiseObject::shiftW(const float p_W)
{
    if (noiseHotReload && p_W != 0.f)
    {
        noiseNeedsRebuild = true;
        if (includeNormal)
            normalNeedsRebuild = true;
    }
    noisePushConstants.w += p_W;
}

void NoiseEngine::NoiseObject::drawImgui(const std::string_view p_NoiseName)
{
    if (!m_ShowWindow)
        return;

    ImGui::Begin((std::string("Noise Object (") + p_NoiseName.data() + ")").c_str(), &m_ShowWindow);
    ImGui::Checkbox("Noise Hot Reload", &noiseHotReload);
    if (!noiseHotReload)
    {
        ImGui::DragFloat2("Noise offset", &noisePushConstants.offset.x, 0.01f);
        ImGui::DragFloat("Noise scale", &noisePushConstants.scale, 0.01f);
        ImGui::DragScalar("Octaves", ImGuiDataType_U32, &noisePushConstants.octaves);
        ImGui::DragFloat("Persistence", &noisePushConstants.persistence, 0.1f);
        ImGui::DragFloat("Lacunarity", &noisePushConstants.lacunarity, 0.1f);
        ImGui::DragFloat("Noise W", &noisePushConstants.w, 0.01f);
        if (ImGui::Button("Recompute Noise"))
        {
            noiseNeedsRebuild = true;
        }
    }
    else
    {
        glm::vec2 l_Offset = m_NoiseOffset;
        ImGui::DragFloat2("Noise offset", &l_Offset.x, 0.01f);
        if (l_Offset != m_NoiseOffset)
        {
            m_NoiseOffset = l_Offset;
            noiseNeedsRebuild = true;
        }
        float l_Scale = noisePushConstants.scale;
        ImGui::DragFloat("Noise scale", &l_Scale, 1.f, 1.f, 100.f);
        if (l_Scale != noisePushConstants.scale)
        {
            noisePushConstants.scale = l_Scale;
            noiseNeedsRebuild = true;
        }
        uint32_t l_Octaves = noisePushConstants.octaves;
        ImGui::DragScalar("Octaves", ImGuiDataType_U32, &l_Octaves);
        if (l_Octaves != noisePushConstants.octaves)
        {
            noisePushConstants.octaves = l_Octaves;
            noiseNeedsRebuild = true;
        }
        float l_Persistence = noisePushConstants.persistence;
        ImGui::DragFloat("Persistence", &l_Persistence, 0.1f, 0.1f, 10.f);
        if (l_Persistence != noisePushConstants.persistence)
        {
            noisePushConstants.persistence = l_Persistence;
            noiseNeedsRebuild = true;
        }
        float l_Lacunarity = noisePushConstants.lacunarity;
        ImGui::DragFloat("Lacunarity", &l_Lacunarity, 0.1f, 0.1f, 10.f);
        if (l_Lacunarity != noisePushConstants.lacunarity)
        {
            noisePushConstants.lacunarity = l_Lacunarity;
            noiseNeedsRebuild = true;
        }
        float l_W = m_W;
        ImGui::DragFloat("Noise W", &l_W, 0.01f);
        if (l_W != m_W)
        {
            m_W = l_W;
            noiseNeedsRebuild = true;
        }
        ImGui::Checkbox("Animated W", &m_WAnimated);
        if (m_WAnimated)
        {
            ImGui::DragFloat("W speed", &m_WSpeed, 0.01f);

            m_WOffset += m_WSpeed * ImGui::GetIO().DeltaTime;
            noisePushConstants.w = m_W + m_WOffset;
            noiseNeedsRebuild = true;
        }
    }

    ImGui::Separator();

    if (includeNormal)
    {
        ImGui::Checkbox("Normal Hot Reload", &normalHotReload);
        if (!normalHotReload)
        {
            ImGui::DragFloat("Normal offset", &normalPushConstants.offsetScale, 0.001f, 0.001f, 0.1f);
            if (ImGui::Button("Recompute Normal"))
            {
                normalNeedsRebuild = true;
            }
        }
        else
        {
            float l_OffsetScale = normalPushConstants.offsetScale;
            ImGui::DragFloat("Normal offset", &l_OffsetScale, 0.001f, 0.01f, 0.1f);
            if (l_OffsetScale != normalPushConstants.offsetScale)
            {
                normalPushConstants.offsetScale = l_OffsetScale;
                normalNeedsRebuild = true;
            }
        }

        ImGui::Separator();
    }

    // ComboBox
    ImGui::BeginDisabled(!includeNormal);
    constexpr std::array<const char*, 2> l_ImageNames = { "Noise", "Normal" };
    if (ImGui::BeginCombo("Image", l_ImageNames[m_ImagePreview]))
    {
        for (int i = 0; i < l_ImageNames.size(); i++)
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
    ImGui::EndDisabled();

    switch (m_ImagePreview)
    {
    case HEIGHTMAP:
        if (imguiHeightmapDescriptorSet == VK_NULL_HANDLE)
            break;
        {
            ImGui::Image(reinterpret_cast<ImTextureID>(imguiHeightmapDescriptorSet), ImVec2(256, 256));
        }
        break;
    case NORMALMAP:
        if (imguiNormalmapDescriptorSet == VK_NULL_HANDLE)
            break;
        {
            ImGui::Image(reinterpret_cast<ImTextureID>(imguiNormalmapDescriptorSet), ImVec2(256, 256));
        }
        break;
    }

    ImGui::End();
}

void NoiseEngine::NoiseObject::cleanupImgui()
{
    ImGui_ImplVulkan_RemoveTexture(imguiHeightmapDescriptorSet);
    imguiHeightmapDescriptorSet = VK_NULL_HANDLE;

    if (includeNormal)
    {
        ImGui_ImplVulkan_RemoveTexture(imguiNormalmapDescriptorSet);
        imguiNormalmapDescriptorSet = VK_NULL_HANDLE;
    }

}

void NoiseEngine::initialize()
{
    VulkanDevice& l_Device = m_Engine.getDevice();

    {
        std::array<VkDescriptorSetLayoutBinding, 1> l_Bindings;
        l_Bindings[0].binding = 0;
        l_Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        l_Bindings[0].descriptorCount = 1;
        l_Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        l_Bindings[0].pImmutableSamplers = nullptr;

        m_ComputeNoiseDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
    }

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

    {
        std::array<VkPushConstantRange, 1> l_ComputeNoisePushConstantRanges;
        l_ComputeNoisePushConstantRanges[0] = { VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NoisePushConstantData)} };
        std::array<ResourceID, 1> l_ComputeDescriptorSetLayouts = { m_ComputeNoiseDescriptorSetLayoutID };
        m_ComputeNoisePipelineLayoutID = l_Device.createPipelineLayout(l_ComputeDescriptorSetLayouts, l_ComputeNoisePushConstantRanges);

        const uint32_t l_ComputeShaderID = l_Device.createShader("shaders/noise.comp", VK_SHADER_STAGE_COMPUTE_BIT, false, {});
        m_ComputeNoisePipelineID = l_Device.createComputePipeline(m_ComputeNoisePipelineLayoutID, l_ComputeShaderID, "main");

        l_Device.freeShader(l_ComputeShaderID);
    }

    {
        std::array<VkPushConstantRange, 1> l_ComputeNormalPushConstantRanges;
        l_ComputeNormalPushConstantRanges[0] = { VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NormalPushConstantData)} };
        std::array<ResourceID, 1> l_ComputeNormalDescriptorSetLayouts = { m_ComputeNormalDescriptorSetLayoutID };
        m_ComputeNormalPipelineLayoutID = l_Device.createPipelineLayout(l_ComputeNormalDescriptorSetLayouts, l_ComputeNormalPushConstantRanges);

        const uint32_t l_ComputeShader = l_Device.createShader("shaders/normal.comp", VK_SHADER_STAGE_COMPUTE_BIT, false, {});
        m_ComputeNormalPipelineID = l_Device.createComputePipeline(m_ComputeNormalPipelineLayoutID, l_ComputeShader, "main");

        l_Device.freeShader(l_ComputeShader);
    }
}

bool NoiseEngine::recalculate(VulkanCommandBuffer& p_CmdBuffer, NoiseObject& p_Object) const
{
    const bool l_CalculatedNoise = recalculateNoise(p_CmdBuffer, p_Object);
    const bool l_CalculatedNormal = recalculateNormal(p_CmdBuffer, p_Object);

    return l_CalculatedNoise || l_CalculatedNormal;
}

bool NoiseEngine::recalculateNoise(VulkanCommandBuffer& p_CmdBuffer, NoiseObject& p_Object) const
{
    if (!p_Object.noiseNeedsRebuild)
        return false;

    if (!p_CmdBuffer.isRecording())
    {
        p_CmdBuffer.reset();
        p_CmdBuffer.beginRecording();
    }

    VulkanDevice& l_Device = m_Engine.getDevice();
    
    VulkanImage& l_HeightmapImage = l_Device.getImage(p_Object.noiseImage.image);
    const VkExtent3D l_ImageSize = l_HeightmapImage.getSize();
    const uint32_t groupCountX = (l_ImageSize.width + 7) / 8;
    const uint32_t groupCountY = (l_ImageSize.height + 7) / 8;

    const uint32_t l_ComputeFamilyIndex = m_Engine.getComputeQueuePos().familyIndex;

    VulkanMemoryBarrierBuilder l_EnterBarrierBuilder{l_Device.getID(), VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
    l_EnterBarrierBuilder.addImageMemoryBarrier(p_Object.noiseImage.image, VK_IMAGE_LAYOUT_GENERAL, l_ComputeFamilyIndex, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    p_CmdBuffer.cmdPipelineBarrier(l_EnterBarrierBuilder);
    l_HeightmapImage.setLayout(VK_IMAGE_LAYOUT_GENERAL);
    l_HeightmapImage.setQueue(l_ComputeFamilyIndex);

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeNoisePipelineID);
    p_CmdBuffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeNoisePipelineLayoutID, p_Object.computeNoiseDescriptorSetID);
    p_CmdBuffer.cmdPushConstant(m_ComputeNoisePipelineLayoutID, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NoisePushConstantData), &p_Object.noisePushConstants);
    p_CmdBuffer.cmdDispatch(groupCountX, groupCountY, 1);

    VulkanMemoryBarrierBuilder l_ExitBarrierBuilder{l_Device.getID(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
    l_ExitBarrierBuilder.addImageMemoryBarrier(p_Object.noiseImage.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, l_ComputeFamilyIndex, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    p_CmdBuffer.cmdPipelineBarrier(l_ExitBarrierBuilder);
    l_HeightmapImage.setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    p_Object.noiseNeedsRebuild = false;

    return true;
}

bool NoiseEngine::recalculateNormal(VulkanCommandBuffer& p_CmdBuffer, NoiseObject& p_Object) const
{
    if (!p_Object.includeNormal || !p_Object.normalNeedsRebuild)
        return false;

    if (!p_CmdBuffer.isRecording())
    {
        p_CmdBuffer.reset();
        p_CmdBuffer.beginRecording();
    }

    VulkanDevice& l_Device = m_Engine.getDevice();

    VulkanImage& l_NormalmapImage = l_Device.getImage(p_Object.normalImage.image);
    const VkExtent3D l_ImageSize = l_NormalmapImage.getSize();
    const uint32_t groupCountX = (l_ImageSize.width + 7) / 8;
    const uint32_t groupCountY = (l_ImageSize.height + 7) / 8;

    const uint32_t l_ComputeFamilyIndex = m_Engine.getComputeQueuePos().familyIndex;
    const uint32_t l_GraphicsFamilyIndex = m_Engine.getGraphicsQueuePos().familyIndex;

    VulkanMemoryBarrierBuilder l_EnterBarrierBuilder{l_Device.getID(), VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
    l_EnterBarrierBuilder.addImageMemoryBarrier(p_Object.normalImage.image, VK_IMAGE_LAYOUT_GENERAL, l_ComputeFamilyIndex, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    p_CmdBuffer.cmdPipelineBarrier(l_EnterBarrierBuilder);
    l_NormalmapImage.setLayout(VK_IMAGE_LAYOUT_GENERAL);
    l_NormalmapImage.setQueue(l_ComputeFamilyIndex);

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeNormalPipelineID);
    p_CmdBuffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeNormalPipelineLayoutID, p_Object.computeNormalDescriptorSetID);
    p_CmdBuffer.cmdPushConstant(m_ComputeNormalPipelineLayoutID, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NormalPushConstantData), &p_Object.normalPushConstants);
    p_CmdBuffer.cmdDispatch(groupCountX, groupCountY, 1);

    VulkanMemoryBarrierBuilder l_ExitBarrierBuilder{l_Device.getID(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, 0};
    l_ExitBarrierBuilder.addImageMemoryBarrier(p_Object.normalImage.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, l_GraphicsFamilyIndex, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    p_CmdBuffer.cmdPipelineBarrier(l_ExitBarrierBuilder);
    l_NormalmapImage.setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    l_NormalmapImage.setQueue(l_GraphicsFamilyIndex);

    p_Object.normalNeedsRebuild = false;

    return true;
}
