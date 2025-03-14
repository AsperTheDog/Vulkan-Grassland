#include "grass_engine.hpp"

#include "camera.hpp"
#include "camera.hpp"
#include "engine.hpp"
#include "vertex.hpp"
#include "vulkan_device.hpp"

void GrassEngine::initalize(const ImageData p_Heightmap, const std::array<uint32_t, 4> p_TileGridSizes, const std::array<uint32_t, 4> p_Densities)
{
    m_HeightmapID = p_Heightmap;

    m_TileGridSizes = p_TileGridSizes;
    m_ImguiGridSizes = p_TileGridSizes;
    m_GrassDensities = p_Densities;
    m_ImguiGrassDensities = p_Densities;

    VulkanDevice& l_Device = m_Engine.getDevice();

    {
        {
            std::array<VkDescriptorSetLayoutBinding, 2> l_Bindings;
            l_Bindings[0].binding = 0;
            l_Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_Bindings[0].descriptorCount = 1;
            l_Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            l_Bindings[0].pImmutableSamplers = nullptr;
            l_Bindings[1].binding = 1;
            l_Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            l_Bindings[1].descriptorCount = 1;
            l_Bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            l_Bindings[1].pImmutableSamplers = nullptr;

            m_ComputeDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
        }

        m_ComputeDescriptorSetID = l_Device.createDescriptorSet(m_Engine.getDescriptorPoolID(), m_ComputeDescriptorSetLayoutID);

        const VkDescriptorImageInfo l_InstanceDataHeightmapInfo{
            .sampler = *l_Device.getImage(m_HeightmapID.image).getSampler(m_HeightmapID.sampler),
            .imageView = *l_Device.getImage(m_HeightmapID.image).getImageView(m_HeightmapID.view),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        std::array<VkWriteDescriptorSet, 1> l_DescriptorWrite{};
    
        l_DescriptorWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_DescriptorWrite[0].dstSet = *l_Device.getDescriptorSet(m_ComputeDescriptorSetID);
        l_DescriptorWrite[0].dstBinding = 0;
        l_DescriptorWrite[0].dstArrayElement = 0;
        l_DescriptorWrite[0].descriptorCount = 1;
        l_DescriptorWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_DescriptorWrite[0].pImageInfo = &l_InstanceDataHeightmapInfo;

        l_Device.updateDescriptorSets(l_DescriptorWrite);
    }

    {
        {
            std::array<VkPushConstantRange, 1> l_PushConstantRanges;
            l_PushConstantRanges[0] = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstantData) };
            std::array<ResourceID, 1> l_DescriptorSetLayouts = { m_ComputeDescriptorSetLayoutID };
            m_ComputePipelineLayoutID = l_Device.createPipelineLayout(l_DescriptorSetLayouts, l_PushConstantRanges);
        }
        const ResourceID l_ShaderID = l_Device.createShader("shaders/grass.comp", VK_SHADER_STAGE_COMPUTE_BIT, false, {});

        m_ComputePipelineID = l_Device.createComputePipeline(m_ComputePipelineLayoutID, l_ShaderID, "main");

        l_Device.freeShader(l_ShaderID);
    }

    {
        {
            std::array<VkPushConstantRange, 1> l_PushConstantRanges;
            l_PushConstantRanges[0] = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) };
            m_GrassPipelineLayoutID = l_Device.createPipelineLayout({}, l_PushConstantRanges);
        }

        const ResourceID l_VertexShaderID = l_Device.createShader("shaders/grass.vert", VK_SHADER_STAGE_VERTEX_BIT, false, {});
        const ResourceID l_FragmentShaderID = l_Device.createShader("shaders/grass.frag", VK_SHADER_STAGE_FRAGMENT_BIT, false, {});

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

        VulkanBinding l_InstanceBinding{ 0, VK_VERTEX_INPUT_RATE_INSTANCE, sizeof(InstanceElem)};
        l_InstanceBinding.addAttribDescription(VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceElem, position));
        l_InstanceBinding.addAttribDescription(VK_FORMAT_R32_SFLOAT, offsetof(InstanceElem, rotation));

        VulkanBinding l_VertexBinding{ 1, VK_VERTEX_INPUT_RATE_VERTEX, sizeof(Vertex) };
        l_VertexBinding.addAttribDescription(VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position));
        l_VertexBinding.addAttribDescription(VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, normal));
        l_VertexBinding.addAttribDescription(VK_FORMAT_R8G8B8_UNORM, offsetof(Vertex, color));

        VulkanPipelineBuilder l_PipelineBuilder{l_Device.getID()};
        l_PipelineBuilder.addVertexBinding(l_InstanceBinding);
        l_PipelineBuilder.addVertexBinding(l_VertexBinding);
        l_PipelineBuilder.setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_FALSE);
        l_PipelineBuilder.setViewportState(1, 1);
        l_PipelineBuilder.setRasterizationState(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        l_PipelineBuilder.setMultisampleState(VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 1.0f);
        l_PipelineBuilder.setDepthStencilState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);
        l_PipelineBuilder.addColorBlendAttachment(l_ColorBlendAttachment);
        l_PipelineBuilder.setColorBlendState(VK_FALSE, VK_LOGIC_OP_COPY, { 0.0f, 0.0f, 0.0f, 0.0f });
        l_PipelineBuilder.setDynamicState(l_DynamicStates);
        l_PipelineBuilder.addShaderStage(l_VertexShaderID, "main");
        l_PipelineBuilder.addShaderStage(l_FragmentShaderID, "main");

        m_GrassPipelineID = l_Device.createPipeline(l_PipelineBuilder, m_GrassPipelineLayoutID, m_Engine.getRenderPassID(), 0);

        l_Device.freeShader(l_VertexShaderID);
        l_Device.freeShader(l_FragmentShaderID);
    }

    // Blade vertex buffers
    {
        auto l_Lerp = [](const float p_A, const float p_B, const float p_T) -> float { return p_A + p_T * (p_B - p_A); };

        // Vertices
        std::array<Vertex, 15> l_BladeVertices;
        for (uint32_t i = 0; i < l_BladeVertices.size() - 1; i += 2)
        {
            const float l_Weight = static_cast<float>(i) / static_cast<float>(l_BladeVertices.size());
            const float l_WeightSq = l_Weight * l_Weight;

            l_BladeVertices[i].position = glm::vec2(l_Lerp(0.1f, 0.0f, l_WeightSq), -l_Weight);
            l_BladeVertices[i].normal = glm::vec2(0.8f, 1.0f);
            l_BladeVertices[i].color = glm::u8vec3(0.0f, l_Lerp(0.1f, 0.6f, l_WeightSq) * UINT8_MAX, 0.0f);

            l_BladeVertices[i + 1].position = l_BladeVertices[i].position;
            l_BladeVertices[i + 1].position.x *= -1.0f;
            l_BladeVertices[i + 1].normal = l_BladeVertices[i].normal;
            l_BladeVertices[i + 1].normal.x = -1.0f;
            l_BladeVertices[i + 1].color = l_BladeVertices[i].color;
        }
        l_BladeVertices.back().position = glm::vec2(0.0f, -1.0f);
        l_BladeVertices.back().normal = glm::vec2(0.0f, 0.0f);
        l_BladeVertices.back().color = glm::u8vec3(0.0f, 0.6f * UINT8_MAX, 0.0f);

        m_VertexBufferData.m_IndexStart = sizeof(l_BladeVertices);

        m_VertexBufferData.m_IndexCounts = { 15, 9, 5, 3 };
        std::array<uint16_t, 15 + 9 + 5 + 3> l_BladeIndices;
        uint32_t l_CurrentIdx = 0;

        // High LOD
        for (uint32_t i = 0; i < 15; i++)
        {
            l_BladeIndices[i] = i;
        }

        l_CurrentIdx += 15;

        // Mid LOD
        uint32_t l_CurrentVtx = 0;
        for (uint32_t i = l_CurrentIdx; i < l_CurrentIdx + 9; i += 2)
        {
            l_BladeIndices[i] = l_CurrentVtx;
            l_BladeIndices[i + 1] = ++l_CurrentVtx;

            l_CurrentVtx += 3;
        }
        l_BladeIndices[15 + 8] = 14;

        l_CurrentIdx += 9;

        // Low LOD
        l_BladeIndices[l_CurrentIdx] = 0;
        l_BladeIndices[l_CurrentIdx + 1] = 1;
        l_BladeIndices[l_CurrentIdx + 2] = 6;
        l_BladeIndices[l_CurrentIdx + 3] = 7;
        l_BladeIndices[l_CurrentIdx + 4] = 14;

        l_CurrentIdx += 5;

        // Very Low LOD
        l_BladeIndices[l_CurrentIdx] = 0;
        l_BladeIndices[l_CurrentIdx + 1] = 1;
        l_BladeIndices[l_CurrentIdx + 2] = 14;

        m_VertexBufferData.m_IndexOffsets = {
            0,
            m_VertexBufferData.m_IndexCounts[0],
            m_VertexBufferData.m_IndexCounts[0] + m_VertexBufferData.m_IndexCounts[1],
            m_VertexBufferData.m_IndexCounts[0] + m_VertexBufferData.m_IndexCounts[1] + m_VertexBufferData.m_IndexCounts[2]
        };

        m_VertexBufferData.m_LODBuffer = l_Device.createBuffer(sizeof(l_BladeVertices) + sizeof(l_BladeIndices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VulkanBuffer& l_LODBuffer = l_Device.getBuffer(m_VertexBufferData.m_LODBuffer);
        l_LODBuffer.allocateFromFlags({VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT});

        {
            void* l_DataPtr = l_Device.mapStagingBuffer(sizeof(l_BladeVertices) + sizeof(l_BladeIndices), 0);
            memcpy(l_DataPtr, l_BladeVertices.data(), sizeof(l_BladeVertices));
            memcpy(static_cast<uint8_t*>(l_DataPtr) + sizeof(l_BladeVertices), l_BladeIndices.data(), sizeof(l_BladeIndices));
            l_Device.dumpStagingBuffer(m_VertexBufferData.m_LODBuffer, sizeof(l_BladeVertices) + sizeof(l_BladeIndices), 0, 0);
        }
    }
}

void GrassEngine::initializeImgui()
{

}

void GrassEngine::update(const glm::vec2 p_CameraTile)
{
    if (m_CurrentTile != p_CameraTile)
    {
        m_CurrentTile = p_CameraTile;
        m_Engine.setGrassDirty();
    }
}

void GrassEngine::updateTileGridSize(const std::array<uint32_t, 4> p_TileGridSizes)
{
    m_TileGridSizes = p_TileGridSizes;
    m_Engine.setGrassDirty();
    m_NeedsRebuild = true;
}

void GrassEngine::updateGrassDensity(const std::array<uint32_t, 4> p_NewDensities)
{
    m_GrassDensities = p_NewDensities;
    m_Engine.setGrassDirty();
    m_NeedsRebuild = true;
}

void GrassEngine::changeCurrentCenter(const glm::ivec2& p_NewCenter)
{
    m_CurrentTile = p_NewCenter;
    m_Engine.setGrassDirty();
}

void GrassEngine::recompute(const VulkanCommandBuffer& p_CmdBuffer, const float p_TileSize, const float p_GridExtent, const float p_HeightmapScale, uint32_t p_GraphicsQueueFamilyIndex)
{
    if (m_NeedsRebuild)
    {
        rebuildResources();
        m_NeedsRebuild = false;
    }

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipelineID);
    p_CmdBuffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipelineLayoutID, m_ComputeDescriptorSetID);
    
    const uint32_t groupCount = (getInstanceCount() + 255) / 256;

    const ComputePushConstantData l_PushConstants{
        .centerPos = m_CurrentTile,
        .worldOffset = glm::vec2(m_CurrentTile) - glm::vec2(p_GridExtent / 2.0),
        .tileGridSizes = glm::uvec4(m_TileGridSizes[0], m_TileGridSizes[1], m_TileGridSizes[2], m_TileGridSizes[3]),
        .tileDensities = glm::uvec4(m_GrassDensities[0], m_GrassDensities[1], m_GrassDensities[2], m_GrassDensities[3]),
        .tileSize = p_TileSize,
        .gridExtent = p_GridExtent,
        .heightmapScale = p_HeightmapScale
    };

    VulkanBuffer& l_InstanceDataBuffer = m_Engine.getDevice().getBuffer(m_InstanceDataBufferID);

    VulkanMemoryBarrierBuilder l_BufferBarrierEnter{m_Engine.getDevice().getID(), VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
    l_BufferBarrierEnter.addBufferMemoryBarrier(m_InstanceDataBufferID, 0, VK_WHOLE_SIZE, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT, m_Engine.getComputeQueuePos().familyIndex);
    p_CmdBuffer.cmdPipelineBarrier(l_BufferBarrierEnter);
    l_InstanceDataBuffer.setQueue(m_Engine.getComputeQueuePos().familyIndex);

    p_CmdBuffer.cmdPushConstant(m_ComputePipelineLayoutID, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstantData), &l_PushConstants);
    p_CmdBuffer.cmdDispatch(groupCount, 1, 1);

    VulkanMemoryBarrierBuilder l_BufferBarrierExit{m_Engine.getDevice().getID(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0};
    l_BufferBarrierExit.addBufferMemoryBarrier(m_InstanceDataBufferID, 0, VK_WHOLE_SIZE, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, m_Engine.getGraphicsQueuePos().familyIndex);
    p_CmdBuffer.cmdPipelineBarrier(l_BufferBarrierExit);
    l_InstanceDataBuffer.setQueue(m_Engine.getGraphicsQueuePos().familyIndex);

    // Transition height and normal maps for rendering

    VulkanDevice& l_Device = m_Engine.getDevice();
    VulkanImage& l_HeightmapImage = l_Device.getImage(m_HeightmapID.image);

    VulkanMemoryBarrierBuilder l_ExitBarrierBuilder2{m_Engine.getDevice().getID(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0};
    l_ExitBarrierBuilder2.addImageMemoryBarrier(m_HeightmapID.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, p_GraphicsQueueFamilyIndex, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
    p_CmdBuffer.cmdPipelineBarrier(l_ExitBarrierBuilder2);
    l_HeightmapImage.setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    l_HeightmapImage.setQueue(p_GraphicsQueueFamilyIndex);
}

void GrassEngine::render(const VulkanCommandBuffer&  p_CmdBuffer) const
{
    const glm::mat4& l_VPMatrix = m_Engine.getCamera().getVPMatrix();
    const std::array<uint32_t, 4> l_InstanceCounts = getInstanceCounts();

    const std::array<ResourceID, 2 > l_Buffers = { m_InstanceDataBufferID, m_VertexBufferData.m_LODBuffer };
    constexpr std::array<VkDeviceSize, 2> l_Offsets = { 0, 0 };

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, m_GrassPipelineID);
    p_CmdBuffer.cmdBindVertexBuffers(l_Buffers, l_Offsets);
    p_CmdBuffer.cmdBindIndexBuffer(m_VertexBufferData.m_LODBuffer, m_VertexBufferData.m_IndexStart, VK_INDEX_TYPE_UINT16);
    p_CmdBuffer.cmdPushConstant(m_GrassPipelineLayoutID, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &l_VPMatrix);

    uint32_t l_Offset = 0;
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (l_InstanceCounts[i] == 0)
            continue;
        p_CmdBuffer.cmdDrawIndexed(m_VertexBufferData.m_IndexCounts[i], m_VertexBufferData.m_IndexOffsets[i], 0, l_InstanceCounts[i], l_Offset);
        l_Offset += l_InstanceCounts[i];
    }
}

void GrassEngine::drawImgui()
{
    ImGui::Begin("Grass");

    ImGui::InputScalar("Grid Rings Close", ImGuiDataType_U32, &m_ImguiGridSizes[0]);
    ImGui::InputScalar("Grid Rings Medium", ImGuiDataType_U32, &m_ImguiGridSizes[1]);
    ImGui::InputScalar("Grid Rings Far", ImGuiDataType_U32, &m_ImguiGridSizes[2]);
    ImGui::InputScalar("Grid Rings Distant", ImGuiDataType_U32, &m_ImguiGridSizes[3]);
    if (ImGui::Button("Update##Radius"))
        updateTileGridSize(m_ImguiGridSizes);

    ImGui::Separator();

    ImGui::InputScalar("Grass Density Close", ImGuiDataType_U32, &m_ImguiGrassDensities[0]);
    ImGui::InputScalar("Grass Density Medium", ImGuiDataType_U32, &m_ImguiGrassDensities[1]);
    ImGui::InputScalar("Grass Density Far", ImGuiDataType_U32, &m_ImguiGrassDensities[2]);
    ImGui::InputScalar("Grass Density Distant", ImGuiDataType_U32, &m_ImguiGrassDensities[3]);
    if (ImGui::Button("Update##Density"))
        updateGrassDensity(m_ImguiGrassDensities);

    ImGui::End();
}

uint32_t GrassEngine::getInstanceCount() const
{
    const std::array<uint32_t, 4> l_InstanceCounts = getInstanceCounts();
    return l_InstanceCounts[0] + l_InstanceCounts[1] + l_InstanceCounts[2] + l_InstanceCounts[3];
}

std::array<uint32_t, 4> GrassEngine::getInstanceCounts() const
{
    const uint32_t l_CloseTiles = m_TileGridSizes[0] * m_TileGridSizes[0];
    const uint32_t l_MediumTiles = m_TileGridSizes[1] * m_TileGridSizes[1] - l_CloseTiles;
    const uint32_t l_FarTiles = m_TileGridSizes[2] * m_TileGridSizes[2] - l_CloseTiles - l_MediumTiles;
    const uint32_t l_DistantTiles = m_TileGridSizes[3] * m_TileGridSizes[3] - l_CloseTiles - l_MediumTiles - l_FarTiles;

    const uint32_t l_CloseGrassInTile = m_GrassDensities[0] * m_GrassDensities[0];
    const uint32_t l_MediumGrassInTile = m_GrassDensities[1] * m_GrassDensities[1];
    const uint32_t l_FarGrassInTile = m_GrassDensities[2] * m_GrassDensities[2];
    const uint32_t l_DistantGrassInTile = m_GrassDensities[3] * m_GrassDensities[3];

    return { l_CloseTiles * l_CloseGrassInTile, l_MediumTiles * l_MediumGrassInTile, l_FarTiles * l_FarGrassInTile, l_DistantTiles * l_DistantGrassInTile };
}

void GrassEngine::rebuildResources()
{
    VulkanDevice& l_Device = m_Engine.getDevice();

    if (m_InstanceDataBufferID != UINT32_MAX)
        l_Device.freeBuffer(m_InstanceDataBufferID);

    m_InstanceDataBufferID = l_Device.createBuffer(sizeof(InstanceElem) * getInstanceCount(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_Engine.getComputeQueuePos().familyIndex);
    VulkanBuffer& l_InstanceDataBuffer = l_Device.getBuffer(m_InstanceDataBufferID);
    l_InstanceDataBuffer.allocateFromFlags({VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false});
    l_InstanceDataBuffer.setQueue(m_Engine.getComputeQueuePos().familyIndex);

    const VkDescriptorBufferInfo l_InstanceDataBufferInfo{
        .buffer = *l_InstanceDataBuffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    const std::array<VkWriteDescriptorSet, 1> l_DescriptorWrite{ VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = *l_Device.getDescriptorSet(m_ComputeDescriptorSetID),
        .dstBinding = 1,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &l_InstanceDataBufferInfo,
    }};

    l_Device.updateDescriptorSets(l_DescriptorWrite);
}
