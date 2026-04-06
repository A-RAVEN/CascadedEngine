#include <VulkanObjects/VulkanShaderStruct.h>
#include <RenderBackend_Vulkan.h>

namespace graphics_backend
{
	void VulkanShaderStruct::Init(ShaderCompilerSlang::ShaderStructData const* pStructData)
	{
		p_StructData = pStructData;
		m_StructTypeName = pStructData->m_TypeName;

		// Initialize uniform staging buffer (Phase 6.4)
		uint64_t offset = 0;
		m_UniformElementOffsetInStagingBuffer.resize(pStructData->m_StructUniforms.m_Elements.size());
		for (uint32_t i = 0; i < pStructData->m_StructUniforms.m_Elements.size(); ++i)
		{
			auto& elementMeta = pStructData->m_StructUniforms.m_Elements[i];
			m_NameToUniformElementMetaID[elementMeta.m_Name] = i;
			m_UniformElementOffsetInStagingBuffer[i] = offset;
			offset += elementMeta.GetFullSize();
		}
		m_StructLocalUniformStagingBuffer.resize(offset);

		// Create a basic descriptor set layout for now
		// In a full implementation, this would be populated from shader reflection
		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = 0;
		layoutInfo.pBindings = nullptr;

		m_DescriptorSetLayout = GetDevice().createDescriptorSetLayout(layoutInfo);

		// Create pipeline layout
		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;

		m_PipelineLayout = GetDevice().createPipelineLayout(pipelineLayoutInfo);

		// Create descriptor pool
		vk::DescriptorPoolSize poolSizes[] = {
			{ vk::DescriptorType::eUniformBuffer, 100 },
			{ vk::DescriptorType::eCombinedImageSampler, 100 },
			{ vk::DescriptorType::eStorageBuffer, 100 },
		};

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.maxSets = 100;
		poolInfo.poolSizeCount = 3;
		poolInfo.pPoolSizes = poolSizes;

		m_DescriptorPool = GetDevice().createDescriptorPool(poolInfo);

		// Allocate descriptor set
		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_DescriptorSetLayout;

		auto sets = GetDevice().allocateDescriptorSets(allocInfo);
		if (!sets.empty())
		{
			m_DescriptorSet = sets[0];
		}

		CA_LOG_INFO("VulkanShaderStruct initialized for type: {}", m_StructTypeName.ToString().c_str());
	}

	void VulkanShaderStruct::Release()
	{
		auto device = GetDevice();

		if (m_DescriptorSet)
		{
			device.freeDescriptorSets(m_DescriptorPool, m_DescriptorSet);
			m_DescriptorSet = nullptr;
		}

		if (m_DescriptorPool)
		{
			device.destroyDescriptorPool(m_DescriptorPool);
			m_DescriptorPool = nullptr;
		}

		if (m_PipelineLayout)
		{
			device.destroyPipelineLayout(m_PipelineLayout);
			m_PipelineLayout = nullptr;
		}

		if (m_DescriptorSetLayout)
		{
			device.destroyDescriptorSetLayout(m_DescriptorSetLayout);
			m_DescriptorSetLayout = nullptr;
		}

		m_PendingUpdates.clear();
		m_StructLocalUniformStagingBuffer.clear();
		m_NameToUniformElementMetaID.clear();
		m_UniformElementOffsetInStagingBuffer.clear();
	}

	void VulkanShaderStruct::SetValueInternal(cacore::NameHash const& name
		, void const* pValue
		, uint32_t sizeInBytes
		, uint32_t elementIndex)
	{
		// Write directly to staging buffer (Phase 6 pattern)
		auto found = m_NameToUniformElementMetaID.find(name);
		if (found != m_NameToUniformElementMetaID.end())
		{
			uint32_t metaID = found->second;
			auto& elementMeta = p_StructData->m_StructUniforms.m_Elements[metaID];
			auto offset = m_UniformElementOffsetInStagingBuffer[metaID];
			CA_ASSERT(elementMeta.m_ElementMemorySize == sizeInBytes, "Size of data does not match size of element in shader struct");
			CA_ASSERT(elementMeta.m_ElementCount > elementIndex, "Element index out of range");
			auto writingOffset = offset + elementMeta.m_Stride * elementIndex;
			CA_ASSERT_BREAK(writingOffset + sizeInBytes <= m_StructLocalUniformStagingBuffer.size(), "Writing Data Exceeds Uniform Staging Buffer");

			memcpy(&m_StructLocalUniformStagingBuffer[writingOffset], pValue, sizeInBytes);
		}
		UpdateVersion();
	}

	void VulkanShaderStruct::SetImageInternal(cacore::NameHash const& name
		, ImageHandle const& imageHandle, GPUTextureView const& view
		, uint32_t elementIndex)
	{
		PendingUpdate update{};
		update.type = PendingUpdate::Type::Image;
		update.name = name;
		update.elementIndex = elementIndex;
		update.imageHandle = imageHandle;
		update.textureView = view;
		m_PendingUpdates.push_back(castl::move(update));
		UpdateVersion();
	}

	void VulkanShaderStruct::SetBufferInternal(cacore::NameHash const& name
		, BufferHandle const& bufferHandle
		, uint32_t elementIndex)
	{
		PendingUpdate update{};
		update.type = PendingUpdate::Type::Buffer;
		update.name = name;
		update.elementIndex = elementIndex;
		update.bufferHandle = bufferHandle;
		m_PendingUpdates.push_back(castl::move(update));
		UpdateVersion();
	}

	void VulkanShaderStruct::SetSamplerInternal(cacore::NameHash const& name
		, TextureSamplerDescriptor const& samplerDesc
		, uint32_t elementIndex)
	{
		PendingUpdate update{};
		update.type = PendingUpdate::Type::Sampler;
		update.name = name;
		update.elementIndex = elementIndex;
		update.samplerDesc = samplerDesc;
		m_PendingUpdates.push_back(castl::move(update));
		UpdateVersion();
	}

	void VulkanShaderStruct::SetStructInternal(cacore::NameHash const& name
		, castl::shared_ptr<ShaderStruct> const& subStruct
		, uint32_t elementIndex)
	{
		PendingUpdate update{};
		update.type = PendingUpdate::Type::Struct;
		update.name = name;
		update.elementIndex = elementIndex;
		update.subStruct = subStruct;
		m_PendingUpdates.push_back(castl::move(update));
		UpdateVersion();
	}

	void VulkanShaderStruct::FlushUpdates()
	{
		// Process pending updates and write to descriptor sets
		// This is a simplified implementation - full version would handle all update types
		for (auto const& update : m_PendingUpdates)
		{
			switch (update.type)
			{
			case PendingUpdate::Type::Value:
				// Write to uniform buffer
				break;
			case PendingUpdate::Type::Image:
				// Write image descriptor
				break;
			case PendingUpdate::Type::Buffer:
				// Write buffer descriptor
				break;
			case PendingUpdate::Type::Sampler:
				// Write sampler descriptor
				break;
			case PendingUpdate::Type::Struct:
				// Handle nested struct
				break;
			}
		}
		m_PendingUpdates.clear();
	}

	void VulkanShaderStruct::UpdateVersion()
	{
		++m_Version;
	}

	uint64_t VulkanShaderStruct::ComputeMaxChildrenVersion() const
	{
		// TODO: When m_NameToSubStructs is added in Phase 7, iterate through sub-structs
		// For now, just return our own version since we don't have sub-structs yet
		m_MaxChildrenVersion = 0;
		return m_Version;
	}

	// Phase 6.5: UpdateUniformBuffer implementation
	void VulkanShaderStruct::UpdateUniformBuffer(uint64_t uniformBufferVersion, void* pOutBuffer, uint32_t bufferSize, uint32_t offset) const
	{
		if (p_StructData == nullptr)
			return;

		// Update Elements (Non Struct)
		if (m_Version > uniformBufferVersion)
		{
			for (uint32_t metaID = 0; metaID < p_StructData->m_StructUniforms.m_Elements.size(); ++metaID)
			{
				auto& elementMeta = p_StructData->m_StructUniforms.m_Elements[metaID];
				auto elementOffset = m_UniformElementOffsetInStagingBuffer[metaID];
				auto writingOffset = offset + elementMeta.m_MemoryOffset;
				CA_ASSERT_BREAK(writingOffset + elementMeta.m_ElementMemorySize * elementMeta.m_ElementCount <= bufferSize, "Writing Data Exceeds Uniform Buffer");
				memcpy(static_cast<uint8_t*>(pOutBuffer) + writingOffset
					, &m_StructLocalUniformStagingBuffer[elementOffset]
					, elementMeta.GetFullSize());
			}
		}

		// Update Child Structs
		if (m_MaxChildrenVersion > uniformBufferVersion)
		{
			// TODO: When Phase 7 adds m_NameToSubStructs, implement sub-struct iteration
			// For now, sub-structs are handled via PendingUpdate mechanism
		}
	}

	uint64_t VulkanShaderStruct::GetCBufferSize() const
	{
		if (p_StructData == nullptr)
			return 0;
		return p_StructData->m_StructUniforms.m_MemorySize;
	}
}
