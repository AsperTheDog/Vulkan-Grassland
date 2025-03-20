#include "grass_engine.hpp"

#include "camera.hpp"
#include "engine.hpp"
#include "vertex.hpp"
#include "vulkan_device.hpp"

void GrassEngine::initalize(const std::array<uint32_t, 4> p_TileGridSizes, const std::array<uint32_t, 4> p_Densities)
{
    m_TileGridSizes = p_TileGridSizes;
    m_ImguiGridSizes = p_TileGridSizes;
    m_GrassDensities = p_Densities;
    m_ImguiGrassDensities = p_Densities;

    recalculateGlobalTilesIndices();
    
    m_HeightNoise.overridePushConstant({
        .scale = 20.f,
        .octaves = 4,
        .persistence = 1.2f,
        .lacunarity = 2.f,
    });
    m_HeightNoise.initialize(512, m_Engine, false);

    m_WindNoise.overridePushConstant({
        .scale = 15.f,
        .octaves = 3,
        .persistence = 1.1f,
        .lacunarity = 1.3f,
    });
    m_WindNoise.initialize(512, m_Engine, false);

    m_LODColors = {
        glm::vec3{1.f, 0.f, 0.f},
        glm::vec3{0.f, 1.f, 0.f},
        glm::vec3{0.f, 0.f, 1.f},
        glm::vec3{1.f, 1.f, 0.f},
    };

    VulkanDevice& l_Device = m_Engine.getDevice();

    {
        {
            std::array<VkDescriptorSetLayoutBinding, 4> l_Bindings;
            l_Bindings[0].binding = 0;
            l_Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_Bindings[0].descriptorCount = 1;
            l_Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            l_Bindings[0].pImmutableSamplers = nullptr;
            l_Bindings[1].binding = 1;
            l_Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_Bindings[1].descriptorCount = 1;
            l_Bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            l_Bindings[1].pImmutableSamplers = nullptr;
            l_Bindings[2].binding = 2;
            l_Bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            l_Bindings[2].descriptorCount = 1;
            l_Bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            l_Bindings[2].pImmutableSamplers = nullptr;
            l_Bindings[3].binding = 3;
            l_Bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            l_Bindings[3].descriptorCount = 1;
            l_Bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            l_Bindings[3].pImmutableSamplers = nullptr;

            m_ComputeDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
        }

        m_ComputeDescriptorSetID = l_Device.createDescriptorSet(m_Engine.getDescriptorPoolID(), m_ComputeDescriptorSetLayoutID);

        const VkDescriptorImageInfo l_InstanceDataHeightmapInfo{
            .sampler = *l_Device.getImage(m_Engine.getHeightmap().noiseImage.image).getSampler(m_Engine.getHeightmap().noiseImage.sampler),
            .imageView = *l_Device.getImage(m_Engine.getHeightmap().noiseImage.image).getImageView(m_Engine.getHeightmap().noiseImage.view),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkDescriptorImageInfo l_InstanceGrassHeightInfo{
            .sampler = *l_Device.getImage(m_HeightNoise.noiseImage.image).getSampler(m_HeightNoise.noiseImage.sampler),
            .imageView = *l_Device.getImage(m_HeightNoise.noiseImage.image).getImageView(m_HeightNoise.noiseImage.view),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        std::array<VkWriteDescriptorSet, 2> l_DescriptorWrite{};
    
        l_DescriptorWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_DescriptorWrite[0].dstSet = *l_Device.getDescriptorSet(m_ComputeDescriptorSetID);
        l_DescriptorWrite[0].dstBinding = 0;
        l_DescriptorWrite[0].dstArrayElement = 0;
        l_DescriptorWrite[0].descriptorCount = 1;
        l_DescriptorWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_DescriptorWrite[0].pImageInfo = &l_InstanceDataHeightmapInfo;
    
        l_DescriptorWrite[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_DescriptorWrite[1].dstSet = *l_Device.getDescriptorSet(m_ComputeDescriptorSetID);
        l_DescriptorWrite[1].dstBinding = 1;
        l_DescriptorWrite[1].dstArrayElement = 0;
        l_DescriptorWrite[1].descriptorCount = 1;
        l_DescriptorWrite[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_DescriptorWrite[1].pImageInfo = &l_InstanceGrassHeightInfo;

        l_Device.updateDescriptorSets(l_DescriptorWrite);
    }

    {
        {
            std::array<VkPushConstantRange, 1> l_PushConstantRanges;
            l_PushConstantRanges[0] = { .stageFlags= VK_SHADER_STAGE_COMPUTE_BIT, .offset= 0, .size = sizeof(ComputePushConstantData) };
            std::array<ResourceID, 1> l_DescriptorSetLayouts = { m_ComputeDescriptorSetLayoutID };
            m_ComputePipelineLayoutID = l_Device.createPipelineLayout(l_DescriptorSetLayouts, l_PushConstantRanges);
        }
        const ResourceID l_ShaderID = l_Device.createShader("shaders/grass.comp", VK_SHADER_STAGE_COMPUTE_BIT, false, {});

        m_ComputePipelineID = l_Device.createComputePipeline(m_ComputePipelineLayoutID, l_ShaderID, "main");

        l_Device.freeShader(l_ShaderID);
    }

    {
        {
            std::array<VkDescriptorSetLayoutBinding, 1> l_Bindings;
            l_Bindings[0].binding = 0;
            l_Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_Bindings[0].descriptorCount = 1;
            l_Bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            l_Bindings[0].pImmutableSamplers = nullptr;

            m_GrassDescriptorSetLayoutID = l_Device.createDescriptorSetLayout(l_Bindings, 0);
        }

        m_GrassDescriptorSetID = l_Device.createDescriptorSet(m_Engine.getDescriptorPoolID(), m_GrassDescriptorSetLayoutID);

        const VkDescriptorImageInfo l_GrassWindInfo{
            .sampler = *l_Device.getImage(m_WindNoise.noiseImage.image).getSampler(m_WindNoise.noiseImage.sampler),
            .imageView = *l_Device.getImage(m_WindNoise.noiseImage.image).getImageView(m_WindNoise.noiseImage.view),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        std::array<VkWriteDescriptorSet, 1> l_DescriptorWrite{};
        l_DescriptorWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        l_DescriptorWrite[0].dstSet = *l_Device.getDescriptorSet(m_GrassDescriptorSetID);
        l_DescriptorWrite[0].dstBinding = 0;
        l_DescriptorWrite[0].dstArrayElement = 0;
        l_DescriptorWrite[0].descriptorCount = 1;
        l_DescriptorWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_DescriptorWrite[0].pImageInfo = &l_GrassWindInfo;

        l_Device.updateDescriptorSets(l_DescriptorWrite);
    }

    {
        {
            std::array<VkPushConstantRange, 2> l_PushConstantRanges;
            l_PushConstantRanges[0] = { VK_SHADER_STAGE_VERTEX_BIT, GrassPushConstantData::getVertexShaderOffset(), GrassPushConstantData::getVertexShaderSize() };
            l_PushConstantRanges[1] = { VK_SHADER_STAGE_FRAGMENT_BIT, GrassPushConstantData::getFragmentShaderOffset(), GrassPushConstantData::getFragmentShaderSize() };
            std::array<ResourceID, 1> l_DescriptorSetLayouts = { m_GrassDescriptorSetLayoutID };
            m_GrassPipelineLayoutID = l_Device.createPipelineLayout(l_DescriptorSetLayouts, l_PushConstantRanges);
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
        l_InstanceBinding.addAttribDescription(VK_FORMAT_R32G32_SFLOAT, offsetof(InstanceElem, uv));
        l_InstanceBinding.addAttribDescription(VK_FORMAT_R32_SFLOAT, offsetof(InstanceElem, height));

        VulkanBinding l_VertexBinding{ 1, VK_VERTEX_INPUT_RATE_VERTEX, sizeof(Vertex) };
        l_VertexBinding.addAttribDescription(VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position));

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

            l_BladeVertices[i + 1].position = l_BladeVertices[i].position;
            l_BladeVertices[i + 1].position.x *= -1.0f;
        }
        l_BladeVertices.back().position = glm::vec2(0.0f, -1.0f);

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
            const ResourceID l_OneTimeTransferCmdBufferID = l_Device.createOneTimeCommandBuffer(m_Engine.getTransferQueuePos().familyIndex);
            VulkanCommandBuffer& l_CmdBuffer = l_Device.getCommandBuffer(l_OneTimeTransferCmdBufferID, 0);

            l_CmdBuffer.beginRecording();

            void* l_DataPtr = l_Device.mapStagingBuffer(sizeof(l_BladeVertices) + sizeof(l_BladeIndices), 0);
            memcpy(l_DataPtr, l_BladeVertices.data(), sizeof(l_BladeVertices));
            memcpy(static_cast<uint8_t*>(l_DataPtr) + sizeof(l_BladeVertices), l_BladeIndices.data(), sizeof(l_BladeIndices));
            l_CmdBuffer.ecmdDumpStagingBuffer(m_VertexBufferData.m_LODBuffer, sizeof(l_BladeVertices) + sizeof(l_BladeIndices), 0);

            l_CmdBuffer.endRecording();

            const VulkanQueue l_Queue = l_Device.getQueue(m_Engine.getTransferQueuePos());
            l_CmdBuffer.submit(l_Queue, {}, {});

            // Dirty, but it's at init so it's fine
            // Staging buffer is used again for something else
            // TODO: Probably a good idea to simply make a different buffer for this instead of waiting
            l_Device.waitIdle();

            l_Device.freeCommandBuffer(l_OneTimeTransferCmdBufferID, 0);
        }
    }
}

void GrassEngine::initializeImgui()
{
    m_HeightNoise.initializeImgui();
    m_WindNoise.initializeImgui();
}

void GrassEngine::cleanupImgui()
{
    m_HeightNoise.cleanupImgui();
    m_WindNoise.cleanupImgui();
}

void GrassEngine::update(const glm::ivec2 p_CameraTile, const float p_HeightmapScale, const float p_TileSize)
{
    m_PushConstants.vpMatrix = m_Engine.getCamera().getVPMatrix();
    m_PushConstants.cameraPos = m_Engine.getCamera().getPosition();
    m_PushConstants.lightDir = m_Engine.getLightDir();

    if (m_CurrentTile != p_CameraTile)
    {
        m_CurrentTile = p_CameraTile;
        
        m_NeedsUpdate = true;
    }

    if (m_HeightNoise.isNoiseDirty())
        m_NeedsUpdate = true;

    m_PushConstants.windDir = glm::normalize(glm::vec2(glm::sin(m_ImguiWindDirection), glm::cos(m_ImguiWindDirection)));

    m_WindOffset += m_PushConstants.windDir * m_ImguiWindSpeed * ImGui::GetIO().DeltaTime; 
    m_WindNoise.updateOffset(m_TileOffset + m_WindOffset);

    if (m_ImguiWAnimated)
        m_WindNoise.shiftW(m_ImguiWindWSpeed * ImGui::GetIO().DeltaTime * m_ImguiWindSpeed);

    if (m_Engine.getCamera().isFrustumDirty())
        m_NeedsCullingUpdate = true;

    recalculateCulling(p_HeightmapScale, p_TileSize);
}

void GrassEngine::updateTileGridSize(const std::array<uint32_t, 4> p_TileGridSizes)
{
    m_TileGridSizes = p_TileGridSizes;
    m_NeedsUpdate = true;
    m_NeedsInstanceRebuild = true;
	m_NeedsTileRebuild = true;
}

void GrassEngine::updateGrassDensity(const std::array<uint32_t, 4> p_NewDensities)
{
    m_GrassDensities = p_NewDensities;
    m_NeedsUpdate = true;
    m_NeedsInstanceRebuild = true;
}

void GrassEngine::changeCurrentCenter(const glm::ivec2 p_NewCenter, const glm::vec2 p_Offset)
{
    m_CurrentTile = p_NewCenter;
    m_TileOffset = p_Offset;
    m_HeightNoise.updateOffset(m_TileOffset);
    m_NeedsUpdate = true;
}

bool GrassEngine::recompute(VulkanCommandBuffer& p_CmdBuffer, const float p_TileSize, const uint32_t p_GridSize, const float p_HeightmapScale)
{
    if (!m_NeedsUpdate)
        return false;

    rebuildInstanceResources();

    if (!p_CmdBuffer.isRecording())
    {
        p_CmdBuffer.reset();
        p_CmdBuffer.beginRecording();
    }

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipelineID);
    p_CmdBuffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipelineLayoutID, m_ComputeDescriptorSetID);
    
    const uint32_t groupCount = (getPostCullInstanceCount() + 255) / 256;

    const ComputePushConstantData l_PushConstants{
        .centerPos = m_CurrentTile,
        .worldOffset = glm::vec2(m_CurrentTile) - (glm::vec2(p_GridSize / 2) * p_TileSize),
        .tileGridSizes = glm::uvec4(m_TileGridSizes[0], m_TileGridSizes[1], m_TileGridSizes[2], m_TileGridSizes[3]),
        .tileDensities = glm::uvec4(m_GrassDensities[0], m_GrassDensities[1], m_GrassDensities[2], m_GrassDensities[3]),
        .tileSize = p_TileSize,
        .gridExtent = p_TileSize * p_GridSize,
        .heightmapScale = p_HeightmapScale,
        .grassBaseHeight = m_ImguiGrassBaseHeight,
        .grassHeightVariation = m_ImguiGrassHeightVariation
    };

    VulkanBuffer& l_InstanceDataBuffer = m_Engine.getDevice().getBuffer(m_InstanceDataBufferID);

    VulkanMemoryBarrierBuilder l_BufferBarrierEnter{m_Engine.getDevice().getID(), VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0};
    l_BufferBarrierEnter.addBufferMemoryBarrier(m_InstanceDataBufferID, 0, VK_WHOLE_SIZE, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT, m_Engine.getComputeQueuePos().familyIndex);
    p_CmdBuffer.cmdPipelineBarrier(l_BufferBarrierEnter);
    l_InstanceDataBuffer.setQueue(m_Engine.getComputeQueuePos().familyIndex);

    m_DebugComputeThreads = groupCount * 256;
    p_CmdBuffer.cmdPushConstant(m_ComputePipelineLayoutID, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstantData), &l_PushConstants);
    p_CmdBuffer.cmdDispatch(groupCount, 1, 1);

    VulkanMemoryBarrierBuilder l_BufferBarrierExit{m_Engine.getDevice().getID(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0};
    l_BufferBarrierExit.addBufferMemoryBarrier(m_InstanceDataBufferID, 0, VK_WHOLE_SIZE, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, m_Engine.getGraphicsQueuePos().familyIndex);
    p_CmdBuffer.cmdPipelineBarrier(l_BufferBarrierExit);
    l_InstanceDataBuffer.setQueue(m_Engine.getGraphicsQueuePos().familyIndex);

    VulkanDevice& l_Device = m_Engine.getDevice();
    VulkanImage& l_HeightmapImage = l_Device.getImage(m_Engine.getHeightmap().noiseImage.image);

    VulkanMemoryBarrierBuilder l_ExitBarrierBuilder2{m_Engine.getDevice().getID(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0};
    l_ExitBarrierBuilder2.addImageMemoryBarrier(m_Engine.getHeightmap().noiseImage.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_Engine.getGraphicsQueuePos().familyIndex, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
    p_CmdBuffer.cmdPipelineBarrier(l_ExitBarrierBuilder2);
    l_HeightmapImage.setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    l_HeightmapImage.setQueue(m_Engine.getGraphicsQueuePos().familyIndex);

    m_NeedsUpdate = false;

    return true;
}

bool GrassEngine::recomputeWind(VulkanCommandBuffer& p_CmdBuffer)
{
    return m_Engine.getNoiseEngine().recalculate(p_CmdBuffer, m_WindNoise);
}

bool GrassEngine::recomputeHeight(VulkanCommandBuffer& p_CmdBuffer)
{
    const bool l_RecomputedHeight = m_Engine.getNoiseEngine().recalculate(p_CmdBuffer, m_HeightNoise);
    if (l_RecomputedHeight)
        m_NeedsUpdate = true;
    return l_RecomputedHeight;
}

void GrassEngine::render(const VulkanCommandBuffer&  p_CmdBuffer)
{
    if (!m_RenderEnabled) return;

    const std::array<uint32_t, 4> l_InstanceCounts = getPostCullInstanceCounts();

    const std::array<ResourceID, 2 > l_Buffers = { m_InstanceDataBufferID, m_VertexBufferData.m_LODBuffer };
    constexpr std::array<VkDeviceSize, 2> l_Offsets = { 0, 0 };

    p_CmdBuffer.cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, m_GrassPipelineID);
    p_CmdBuffer.cmdBindVertexBuffers(l_Buffers, l_Offsets);
    p_CmdBuffer.cmdBindIndexBuffer(m_VertexBufferData.m_LODBuffer, m_VertexBufferData.m_IndexStart, VK_INDEX_TYPE_UINT16);
    p_CmdBuffer.cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, m_GrassPipelineLayoutID, m_GrassDescriptorSetID);

    const glm::vec3 l_BaseColor = m_PushConstants.baseColor;
    const glm::vec3 l_TipColor = m_PushConstants.tipColor;

    uint32_t l_Offset = 0;
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (l_InstanceCounts[i] == 0)
            continue;

        if (m_RandomizeLODColors)
        {
            m_PushConstants.baseColor = glm::vec3(0.0f, 0.0f, 0.0f);
            m_PushConstants.tipColor = m_LODColors[i];
        }
        
        m_PushConstants.widthMult = m_GrassWidths[i];
        m_DebugInstanceCalls[i] = l_InstanceCounts[i];
        m_DebugInstanceOffsets[i] = l_Offset;
        p_CmdBuffer.cmdPushConstant(m_GrassPipelineLayoutID, VK_SHADER_STAGE_VERTEX_BIT, GrassPushConstantData::getVertexShaderOffset(), GrassPushConstantData::getVertexShaderSize(), m_PushConstants.getVertexShaderData());
        p_CmdBuffer.cmdPushConstant(m_GrassPipelineLayoutID, VK_SHADER_STAGE_FRAGMENT_BIT, GrassPushConstantData::getFragmentShaderOffset(), GrassPushConstantData::getFragmentShaderSize(), m_PushConstants.getFragmentShaderData());
        p_CmdBuffer.cmdDrawIndexed(m_VertexBufferData.m_IndexCounts[i], m_VertexBufferData.m_IndexOffsets[i], 0, l_InstanceCounts[i], l_Offset);
        l_Offset += l_InstanceCounts[i];
    }

    m_PushConstants.baseColor = l_BaseColor;
    m_PushConstants.tipColor = l_TipColor;
}

void GrassEngine::drawImgui()
{
    ImGui::Begin("Grass");

    ImGui::Checkbox("Enabled", &m_RenderEnabled);

    ImGui::Separator();

    if (ImGui::Button("Recompute"))
    {
        updateTileGridSize(m_ImguiGridSizes);
        updateGrassDensity(m_ImguiGrassDensities);
    }

    uint32_t l_CloseSize = m_ImguiGridSizes[0];
    ImGui::InputScalar("Grid Rings Close", ImGuiDataType_U32, &l_CloseSize);
    if (l_CloseSize != m_ImguiGridSizes[0])
        m_ImguiGridSizes[0] = std::min(l_CloseSize, m_ImguiGridSizes[1] - 1);
    uint32_t l_MediumSize = m_ImguiGridSizes[1];
    ImGui::InputScalar("Grid Rings Medium", ImGuiDataType_U32, &l_MediumSize);
    if (l_MediumSize != m_ImguiGridSizes[1])
        m_ImguiGridSizes[1] = std::clamp(l_MediumSize, m_ImguiGridSizes[0] + 1, m_ImguiGridSizes[2] - 1);
    uint32_t l_FarSize = m_ImguiGridSizes[2];
    ImGui::InputScalar("Grid Rings Far", ImGuiDataType_U32, &l_FarSize);
    if (l_FarSize != m_ImguiGridSizes[2])
        m_ImguiGridSizes[2] = std::clamp(l_FarSize, m_ImguiGridSizes[1] + 1, m_ImguiGridSizes[3] - 1);
    uint32_t l_DistantSize = m_ImguiGridSizes[3];
    ImGui::InputScalar("Grid Rings Distant", ImGuiDataType_U32, &l_DistantSize);
    if (l_DistantSize != m_ImguiGridSizes[3])
        m_ImguiGridSizes[3] = std::max(l_DistantSize, m_ImguiGridSizes[2] + 1);

    ImGui::Separator();

    ImGui::InputScalar("Grass Density Close", ImGuiDataType_U32, &m_ImguiGrassDensities[0]);
    ImGui::InputScalar("Grass Density Medium", ImGuiDataType_U32, &m_ImguiGrassDensities[1]);
    ImGui::InputScalar("Grass Density Far", ImGuiDataType_U32, &m_ImguiGrassDensities[2]);
    ImGui::InputScalar("Grass Density Distant", ImGuiDataType_U32, &m_ImguiGrassDensities[3]);

    ImGui::Separator();

    ImGui::DragFloat("Grass Base Height", &m_ImguiGrassBaseHeight, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Grass Height Variation", &m_ImguiGrassHeightVariation, 0.01f, 0.0f, 1.0f);
    if (ImGui::Button("Edit Grass Height"))
        m_HeightNoise.toggleImgui();

    ImGui::Separator();

    ImGui::DragFloat("Width Close", &m_GrassWidths[0], 0.01f, 0.01f, 10.0f);
    ImGui::DragFloat("Width Medium", &m_GrassWidths[1], 0.01f, 0.01f, 10.0f);
    ImGui::DragFloat("Width Far", &m_GrassWidths[2], 0.01f, 0.01f, 10.0f);
    ImGui::DragFloat("Width Distant", &m_GrassWidths[3], 0.01f, 0.01f, 10.0f);

    ImGui::Separator();

    ImGui::Checkbox("LOD Random Colors", &m_RandomizeLODColors);
    if (!m_RandomizeLODColors)
    {
        ImGui::ColorEdit3("Base Color", &m_PushConstants.baseColor.x);
        ImGui::ColorEdit3("Tip Color", &m_PushConstants.tipColor.x);
    }
    ImGui::DragFloat("Color Ramp", &m_PushConstants.colorRamp, 0.01f, 0.01f, 10.0f);
    
    ImGui::Separator();

    ImGui::DragFloat("Tilt", &m_PushConstants.tilt, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Tilt Bend", &m_PushConstants.bend, 0.01f, 0.0f, 5.0f);

    ImGui::Separator();

    ImGui::DragFloat("Grass Roundness", &m_PushConstants.grassRoundness, 0.01f, 0.0f, 0.99f);

    ImGui::Separator();

    ImGui::DragFloat("Wind Direction", &m_ImguiWindDirection, 0.1f, 0.0f, 2.0f * glm::pi<float>());
    ImGui::DragFloat("Wind Speed", &m_ImguiWindSpeed, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Wind Strength", &m_PushConstants.windStrength, 0.01f, 0.0f, 3.0f);
    ImGui::Checkbox("Animate Wind W", &m_ImguiWAnimated);
    if (m_ImguiWAnimated)
        ImGui::DragFloat("Wind W Speed", &m_ImguiWindWSpeed, 0.001f, 0.0f, 50.0f);
    if (ImGui::Button("Edit Wind Noise"))
        m_WindNoise.toggleImgui();

    ImGui::Separator();

	ImGui::Text("Rendering %u tiles out of %u (%u instances)", getPostCullTileCount(), getPreCullTileCount(), getPostCullInstanceCount());
    ImGui::Checkbox("Enable Culling", &m_CullingEnable);
    if (!m_CullingEnable)
        m_CullingEnable = false;
    ImGui::Checkbox("Update Culling", &m_CullingUpdate);
    ImGui::DragFloat("Culling Margin", &m_ImguiCullingMargin, 0.1f, 0.0f, 10.0f);

    ImGui::End();


    ImGui::Begin("Grass Debug");

    ImGui::Text("Tile Grid Sizes: %u, %u, %u, %u", m_TileGridSizes[0], m_TileGridSizes[1], m_TileGridSizes[2], m_TileGridSizes[3]);
    ImGui::Text("Grass Densities: %u, %u, %u, %u", m_GrassDensities[0], m_GrassDensities[1], m_GrassDensities[2], m_GrassDensities[3]);
    ImGui::Separator();
    ImGui::Text("Instance buffer size %u (%u)", m_DebugInstanceBufferSize, m_DebugInstanceBufferSize / sizeof(InstanceElem));
    ImGui::Text("Tile buffer size %u (%u)", m_DebugTileBufferSize, (m_DebugTileBufferSize - sizeof(TileBufferHeader)) / sizeof(TileBufferElem));
    ImGui::Text("Compute Threads: %u", m_DebugComputeThreads);
    ImGui::Separator();
    ImGui::Text("Instance Calls: %u, %u, %u, %u", m_DebugInstanceCalls[0], m_DebugInstanceCalls[1], m_DebugInstanceCalls[2], m_DebugInstanceCalls[3]);
    ImGui::Text("Instance Offsets: %u, %u, %u, %u", m_DebugInstanceOffsets[0], m_DebugInstanceOffsets[1], m_DebugInstanceOffsets[2], m_DebugInstanceOffsets[3]);
    ImGui::Separator();
    ImGui::Text("Tile Header: %u, %u, %u, %u", m_DebugTileHeader.tileOffsets[0], m_DebugTileHeader.tileOffsets[1], m_DebugTileHeader.tileOffsets[2], m_DebugTileHeader.tileOffsets[3]);
    ImGui::Text("Instance Header: %u, %u, %u, %u", m_DebugTileHeader.instanceOffsets[0], m_DebugTileHeader.instanceOffsets[1], m_DebugTileHeader.instanceOffsets[2], m_DebugTileHeader.instanceOffsets[3]);

    ImGui::End();


    m_HeightNoise.drawImgui("Grass Height");
    m_WindNoise.drawImgui("Wind");
}

bool GrassEngine::transferCulling(VulkanCommandBuffer& p_CmdBuffer)
{
    if (!m_NeedsTransfer)
        return false;

    rebuildTileResources();

    if (!p_CmdBuffer.isRecording())
    {
        p_CmdBuffer.reset();
        p_CmdBuffer.beginRecording();
    }

    {
        const std::array<uint32_t, 4> l_TileCounts = getPostCullTileCounts();
		const std::array<uint32_t, 4> l_InstanceCounts = getPostCullInstanceCounts();
        const TileBufferHeader l_Header{
            .instanceOffsets = {
			    0,
			    l_InstanceCounts[0],
			    l_InstanceCounts[0] + l_InstanceCounts[1],
			    l_InstanceCounts[0] + l_InstanceCounts[1] + l_InstanceCounts[2]
            },
            .tileOffsets = {
                0,
                l_TileCounts[0],
                l_TileCounts[0] + l_TileCounts[1],
                l_TileCounts[0] + l_TileCounts[1] + l_TileCounts[2]
            }
        };
        m_DebugTileHeader = l_Header;
		VulkanDevice& l_Device = m_Engine.getDevice();
        void* l_DataPtr = l_Device.mapStagingBuffer(sizeof(TileBufferHeader) + sizeof(TileBufferElem) * m_TileVisibilityData.size(), 0);
        memcpy(l_DataPtr, &l_Header, sizeof(TileBufferHeader));
        l_DataPtr = static_cast<uint8_t*>(l_DataPtr) + sizeof(TileBufferHeader);
		memcpy(l_DataPtr, m_TileVisibilityData.data(), sizeof(TileBufferElem) * m_TileVisibilityData.size());
        p_CmdBuffer.ecmdDumpStagingBuffer(m_TileDataBufferID, sizeof(TileBufferHeader) + sizeof(TileBufferElem) * m_TileVisibilityData.size(), 0);
    }

    m_NeedsTransfer = false;
    m_NeedsUpdate = true;

    return true;
}

uint32_t GrassEngine::getPreCullInstanceCount() const
{
    const std::array<uint32_t, 4> l_InstanceCounts = getPreCullInstanceCounts();
    return l_InstanceCounts[0] + l_InstanceCounts[1] + l_InstanceCounts[2] + l_InstanceCounts[3];
}

std::array<uint32_t, 4> GrassEngine::getPreCullInstanceCounts() const
{
    const std::array<uint32_t, 4> l_TileCounts = getPreCullTileCounts();
    return {
        l_TileCounts[0] * m_GrassDensities[0] * m_GrassDensities[0],
        l_TileCounts[1] * m_GrassDensities[1] * m_GrassDensities[1],
        l_TileCounts[2] * m_GrassDensities[2] * m_GrassDensities[2],
        l_TileCounts[3] * m_GrassDensities[3] * m_GrassDensities[3]
    };
}

uint32_t GrassEngine::getPostCullInstanceCount() const
{
    const std::array<uint32_t, 4> l_InstanceCounts = getPostCullInstanceCounts();
    return l_InstanceCounts[0] + l_InstanceCounts[1] + l_InstanceCounts[2] + l_InstanceCounts[3];
}

std::array<uint32_t, 4> GrassEngine::getPostCullInstanceCounts() const
{
    const std::array<uint32_t, 4> l_TileCounts = getPostCullTileCounts();
    return {
        l_TileCounts[0] * m_GrassDensities[0] * m_GrassDensities[0],
        l_TileCounts[1] * m_GrassDensities[1] * m_GrassDensities[1],
        l_TileCounts[2] * m_GrassDensities[2] * m_GrassDensities[2],
        l_TileCounts[3] * m_GrassDensities[3] * m_GrassDensities[3]
    };
}

uint32_t GrassEngine::getPreCullTileCount() const
{
    const std::array<uint32_t, 4> l_TileCounts = getPreCullTileCounts();
    return l_TileCounts[0] + l_TileCounts[1] + l_TileCounts[2] + l_TileCounts[3];
}

std::array<uint32_t, 4> GrassEngine::getPreCullTileCounts() const
{
    const uint32_t l_CloseTiles = m_TileGridSizes[0] * m_TileGridSizes[0];
    const uint32_t l_MediumTiles = m_TileGridSizes[1] * m_TileGridSizes[1] - l_CloseTiles;
    const uint32_t l_FarTiles = m_TileGridSizes[2] * m_TileGridSizes[2] - l_CloseTiles - l_MediumTiles;
    const uint32_t l_DistantTiles = m_TileGridSizes[3] * m_TileGridSizes[3] - l_CloseTiles - l_MediumTiles - l_FarTiles;

    return { l_CloseTiles, l_MediumTiles, l_FarTiles, l_DistantTiles };
}

uint32_t GrassEngine::getPostCullTileCount() const
{
    const std::array<uint32_t, 4>& l_TileCounts = getPostCullTileCounts();
    return l_TileCounts[0] + l_TileCounts[1] + l_TileCounts[2] + l_TileCounts[3];
}

void GrassEngine::recalculateCulling(const float p_HeightmapScale, const float p_TileSize)
{
    if (!m_CullingUpdate)
        return;

    rebuildTileResources();

    if (!m_NeedsCullingUpdate)
        return;

    if (!m_CullingEnable)
        m_Engine.getCamera().recalculateFrustum();
    
    m_TileVisibilityData.clear();
    uint32_t l_Current = 0;
    const std::array<uint32_t, 4> l_TileCounts = getPreCullTileCounts();
    const std::array<uint32_t, 4> l_TileOffsets{
        0,
        l_TileCounts[0],
        l_TileCounts[0] + l_TileCounts[1],
        l_TileCounts[0] + l_TileCounts[1] + l_TileCounts[2]
    };

    const glm::vec2 l_TileShift = glm::vec2((m_TileGridSizes[3] / 2) * p_TileSize);
    for (uint32_t l_LOD = 0; l_LOD < l_TileCounts.size(); l_LOD++)
    {
        const uint32_t l_First = l_Current;
        for (uint32_t l_TileIdx = l_TileOffsets[l_LOD]; l_TileIdx < l_TileOffsets[l_LOD] + l_TileCounts[l_LOD]; l_TileIdx++)
        {
            const uint32_t l_Tile = m_GlobalTilePositions[l_TileIdx];
            if (m_CullingEnable)
            {
                const glm::vec2 l_TilePos = glm::vec2(l_Tile % m_TileGridSizes[3], l_Tile / m_TileGridSizes[3]) * p_TileSize - l_TileShift + glm::vec2(m_CurrentTile);

                glm::vec3 l_AABBMin = glm::vec3(l_TilePos.x, -p_HeightmapScale, l_TilePos.y) - glm::vec3(m_ImguiCullingMargin);
                glm::vec3 l_AABBMax = glm::vec3(l_TilePos.x + p_TileSize, 0.0f, l_TilePos.y + p_TileSize) + glm::vec3(m_ImguiCullingMargin);

                if (!m_Engine.getCamera().isBoxInFrustum(l_AABBMin, l_AABBMax))
                    continue;
            }

            m_TileVisibilityData.emplace_back(l_Tile, l_TileIdx - l_TileOffsets[l_LOD]);
            l_Current++;
        }
        m_PostCullTileCounts[l_LOD] = l_Current - l_First;
    }

    m_NeedsCullingUpdate = false;
    m_NeedsTransfer = true;
}

void GrassEngine::rebuildInstanceResources()
{
	if (!m_NeedsInstanceRebuild)
		return;

    VulkanDevice& l_Device = m_Engine.getDevice();

	if (m_InstanceDataBufferID != UINT32_MAX)
		l_Device.freeBuffer(m_InstanceDataBufferID);

    m_DebugInstanceBufferSize = sizeof(InstanceElem) * getPreCullInstanceCount();
	m_InstanceDataBufferID = l_Device.createBuffer(m_DebugInstanceBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_Engine.getComputeQueuePos().familyIndex);
	VulkanBuffer& l_InstanceDataBuffer = l_Device.getBuffer(m_InstanceDataBufferID);
	l_InstanceDataBuffer.allocateFromFlags({.desiredProperties= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, .undesiredProperties= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, .allowUndesired= false});
	l_InstanceDataBuffer.setQueue(m_Engine.getComputeQueuePos().familyIndex);

    const VkDescriptorBufferInfo l_InstanceDataBufferInfo{
        .buffer = *l_InstanceDataBuffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    const std::array<VkWriteDescriptorSet, 1> l_DescriptorWrite{
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = *l_Device.getDescriptorSet(m_ComputeDescriptorSetID),
            .dstBinding = 3,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &l_InstanceDataBufferInfo,
        }
    };

    l_Device.updateDescriptorSets(l_DescriptorWrite);

    m_NeedsInstanceRebuild = false;
    m_NeedsUpdate = true;
}

void GrassEngine::rebuildTileResources()
{
	if (!m_NeedsTileRebuild)
		return;

    VulkanDevice& l_Device = m_Engine.getDevice();

    if (m_TileDataBufferID != UINT32_MAX)
        l_Device.freeBuffer(m_TileDataBufferID);

    m_DebugTileBufferSize = sizeof(TileBufferHeader) + sizeof(TileBufferElem) * getPreCullTileCount();
    m_TileDataBufferID = l_Device.createBuffer(m_DebugTileBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_Engine.getTransferQueuePos().familyIndex);
    VulkanBuffer& l_TileDataBuffer = l_Device.getBuffer(m_TileDataBufferID);
    l_TileDataBuffer.allocateFromFlags({ .desiredProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, .undesiredProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, .allowUndesired = false });
	l_TileDataBuffer.setQueue(m_Engine.getTransferQueuePos().familyIndex);

	const VkDescriptorBufferInfo l_TileDataBufferInfo{
        .buffer = *l_TileDataBuffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

	const std::array<VkWriteDescriptorSet, 1> l_DescriptorWrite{
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = *l_Device.getDescriptorSet(m_ComputeDescriptorSetID),
            .dstBinding = 2,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &l_TileDataBufferInfo,
        }
    };

    l_Device.updateDescriptorSets(l_DescriptorWrite);

    recalculateGlobalTilesIndices();

    m_NeedsTileRebuild = false;
    m_NeedsCullingUpdate = true;
}

void GrassEngine::recalculateGlobalTilesIndices()
{
    m_GlobalTilePositions.clear();
    m_GlobalTilePositions.reserve(getPreCullTileCount());

    auto l_GetGlobalFromLocal = [](const uint32_t p_InnerRing, const uint32_t p_OuterRing, const uint32_t p_GridSize, const uint32_t p_LocalIndex)
    {
        const uint32_t l_MidSize = p_OuterRing - p_InnerRing;
        const uint32_t l_MidHalfSize = l_MidSize / 2;
        uint32_t l_FullRows = std::min(p_LocalIndex / p_OuterRing, l_MidHalfSize);
        uint32_t l_CurrentIndex = static_cast<uint32_t>(std::max(0, static_cast<int32_t>(p_LocalIndex) - static_cast<int32_t>(l_MidHalfSize * p_OuterRing)));
        const uint32_t l_InnerRowOffset = l_CurrentIndex % l_MidSize;
        const uint32_t l_MidRows = std::min(l_CurrentIndex / l_MidSize, p_InnerRing);
        l_CurrentIndex = std::max(0, static_cast<int32_t>(l_CurrentIndex) - static_cast<int32_t>(p_InnerRing * l_MidSize));
        l_FullRows += l_CurrentIndex / p_OuterRing;

        const uint32_t l_ExternSize = p_GridSize - p_OuterRing;
        const uint32_t l_ExternHalfSize = l_ExternSize / 2;
        const uint32_t l_ExternOffset = (p_GridSize * l_ExternHalfSize) + (l_FullRows + l_MidRows) * l_ExternSize + l_ExternHalfSize;
        const bool l_ExtraRow = l_FullRows == l_MidHalfSize && l_InnerRowOffset >= l_MidHalfSize && l_MidRows < p_InnerRing;
        const uint32_t l_InnerOffset = (l_ExtraRow ? l_MidRows + 1 : l_MidRows) * p_InnerRing;

        return p_LocalIndex + l_ExternOffset + l_InnerOffset;
    };

    const std::array<uint32_t, 4> l_TileCounts = getPreCullTileCounts();

    for (uint32_t i = 0; i < l_TileCounts[0]; ++i)
        m_GlobalTilePositions.push_back(l_GetGlobalFromLocal(0, m_TileGridSizes[0], m_TileGridSizes[3], i));
    for (uint32_t i = 0; i < l_TileCounts[1]; ++i)
        m_GlobalTilePositions.push_back(l_GetGlobalFromLocal(m_TileGridSizes[0], m_TileGridSizes[1], m_TileGridSizes[3], i));
    for (uint32_t i = 0; i < l_TileCounts[2]; ++i)
        m_GlobalTilePositions.push_back(l_GetGlobalFromLocal(m_TileGridSizes[1], m_TileGridSizes[2], m_TileGridSizes[3], i));
    for (uint32_t i = 0; i < l_TileCounts[3]; ++i)
        m_GlobalTilePositions.push_back(l_GetGlobalFromLocal(m_TileGridSizes[2], m_TileGridSizes[3], m_TileGridSizes[3], i));
}
