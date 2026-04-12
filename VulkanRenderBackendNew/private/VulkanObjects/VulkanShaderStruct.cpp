#include <VulkanObjects/VulkanShaderStruct.h>
#include <RenderBackend_Vulkan.h>

namespace graphics_backend
{
	void VulkanShaderStruct::Init(ShaderCompilerSlang::ShaderStructData const* pStructData)
	{
		p_StructData = pStructData;
		if (p_StructData == nullptr)
		{
			CA_LOG_WARN("VulkanShaderStruct::Init - null pStructData");
			return;
		}
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

		// Initialize resource handle maps (Phase 7, matching D3D12 pattern)
		for (auto& subStruct : pStructData->m_SubStructReferences)
		{
			m_NameToSubStructs[subStruct.m_Name].resize(subStruct.m_ElementCount);
		}
		for (auto& image : pStructData->m_Textures)
		{
			m_NameToImageHandles[image.m_Name].resize(image.m_ElementCount);
		}
		for (auto& sampler : pStructData->m_TextureSamplers)
		{
			m_NameToSamplerDescriptors[sampler].resize(1);
		}
		for (auto& buffer : pStructData->m_Buffers)
		{
			m_NameToBufferHandles[buffer.m_Name].resize(buffer.m_ElementCount);
		}

		// TODO: Create descriptor set layout from VulkanShaderResourceBindingInfo
		// Currently creates an empty layout as placeholder
		// Full implementation should look up VulkanShaderFileInfo::shaderBindingInfo.setLayoutInfos
		// and use VulkanDescriptorSetLayoutInfo::GetCreateInfo() for proper layout creation
		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = 0;
		layoutInfo.pBindings = nullptr;

		m_DescriptorSetLayout = GetDevice().createDescriptorSetLayout(layoutInfo);

		// TODO: Pipeline layout should be created from the descriptor set layouts
		// derived from VulkanShaderResourceBindingInfo, not from this empty layout
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

		CA_LOG_INFO("VulkanShaderStruct initialized for type: {}", m_StructTypeName.c_str());
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

		m_StructLocalUniformStagingBuffer.clear();
		m_NameToUniformElementMetaID.clear();
		m_UniformElementOffsetInStagingBuffer.clear();
		m_NameToSubStructs.clear();
		m_NameToImageHandles.clear();
		m_NameToBufferHandles.clear();
		m_NameToSamplerDescriptors.clear();
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
		auto found = m_NameToImageHandles.find(name);
		if (found != m_NameToImageHandles.end())
		{
			auto& imageList = found->second;
			CA_ASSERT(imageList.size() > elementIndex, "");
			imageList[elementIndex] = { imageHandle, view };
		}
		UpdateVersion();
	}

	void VulkanShaderStruct::SetBufferInternal(cacore::NameHash const& name
		, BufferHandle const& bufferHandle
		, uint32_t elementIndex)
	{
		auto found = m_NameToBufferHandles.find(name);
		if (found != m_NameToBufferHandles.end())
		{
			auto& bufferList = found->second;
			CA_ASSERT(bufferList.size() > elementIndex, "");
			bufferList[elementIndex] = bufferHandle;
		}
		UpdateVersion();
	}

	void VulkanShaderStruct::SetSamplerInternal(cacore::NameHash const& name
		, TextureSamplerDescriptor const& samplerDesc
		, uint32_t elementIndex)
	{
		auto found = m_NameToSamplerDescriptors.find(name);
		if (found != m_NameToSamplerDescriptors.end())
		{
			auto& samplerList = found->second;
			CA_ASSERT(samplerList.size() > elementIndex, "");
			samplerList[elementIndex] = samplerDesc;
		}
		UpdateVersion();
	}

	void VulkanShaderStruct::SetStructInternal(cacore::NameHash const& name
		, castl::shared_ptr<ShaderStruct> const& subStruct
		, uint32_t elementIndex)
	{
		auto found = m_NameToSubStructs.find(name);
		if (found != m_NameToSubStructs.end())
		{
			auto& structList = found->second;
			CA_ASSERT(structList.size() > elementIndex, "");
			structList[elementIndex] = castl::static_pointer_cast<VulkanShaderStruct>(subStruct);
		}
		UpdateVersion();
	}

	void VulkanShaderStruct::UpdateVersion()
	{
		++m_Version;
	}

	uint64_t VulkanShaderStruct::ComputeMaxChildrenVersion() const
	{
		m_MaxChildrenVersion = 0;
		for (auto& pair : m_NameToSubStructs)
		{
			for (auto& pStruct : pair.second)
			{
				if (pStruct)
				{
					m_MaxChildrenVersion = castl::max(m_MaxChildrenVersion, pStruct->ComputeMaxChildrenVersion());
				}
			}
		}
		return castl::max(m_Version, m_MaxChildrenVersion);
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
			for (auto& structRef : p_StructData->m_SubStructReferences)
			{
				auto found = m_NameToSubStructs.find(structRef.m_Name);
				CA_ASSERT(found != m_NameToSubStructs.end(), "Sub Struct {} Not Found In Shader Struct {}", structRef.m_Name, p_StructData->m_TypeName);
				if (found != m_NameToSubStructs.end())
				{
					auto& subStructList = found->second;
					for (uint32_t elementID = 0; elementID < structRef.m_ElementCount; ++elementID)
					{
						CA_ASSERT(elementID < subStructList.size(), "Sub Struct {} In Shader Struct {} Has Not Been Set At Element {}", structRef.m_Name, p_StructData->m_TypeName, elementID);
						if (elementID < subStructList.size())
						{
							auto& pSubStruct = subStructList[elementID];
							if (pSubStruct)
							{
								pSubStruct->UpdateUniformBuffer(uniformBufferVersion
									, pOutBuffer
									, bufferSize
									, offset + structRef.m_MemoryOffset + structRef.m_Stride * elementID);
							}
						}
					}
				}
			}
		}
	}

	uint64_t VulkanShaderStruct::GetCBufferSize() const
	{
		if (p_StructData == nullptr)
			return 0;
		return p_StructData->m_StructUniforms.m_MemorySize;
	}

	ETextureAccessType VulkanShaderStruct::GetTextureAccessType(cacore::NameHash const& textureName) const
	{
		if (p_StructData == nullptr)
			return ETextureAccessType::eAccessType_Max;
		for (auto& texture : p_StructData->m_Textures)
		{
			if (texture.m_Name == textureName)
			{
				switch (texture.m_RWType)
				{
				case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
					return ETextureAccessType::eSampled;
				case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
				case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
					return ETextureAccessType::eUnorderedAccess;
				}
			}
		}
		return ETextureAccessType::eAccessType_Max;
	}

	ShaderCompilerSlang::EShaderResourceAccess VulkanShaderStruct::GetBufferRWType(cacore::NameHash const& bufferName) const
	{
		if (p_StructData == nullptr)
			return ShaderCompilerSlang::EShaderResourceAccess::eUnknown;
		for (auto& buffer : p_StructData->m_Buffers)
		{
			if (buffer.m_Name == bufferName)
			{
				return buffer.m_RWType;
			}
		}
		return ShaderCompilerSlang::EShaderResourceAccess::eUnknown;
	}
}
