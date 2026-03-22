#include "D3D12ShaderStruct.h"
#include <RenderBackend_D3D12.h>

namespace graphics_backend
{
	D3D2ShaderStruct::D3D2ShaderStruct(RenderBackend_D3D12* app, ShaderCompilerSlang::ShaderStructData const* pStructData)
		: D3D12SubobjectBase(app)
		, p_StructData(pStructData)
	{
		//m_SelfUniformBuffer.resize(p_StructData->m_StructUniforms.m_MemorySize);
		uint64_t offset = 0;
		m_UniformElementOffsetInStagingBuffer.resize(p_StructData->m_StructUniforms.m_Elements.size());
		for (uint32_t i = 0; i < pStructData->m_StructUniforms.m_Elements.size(); ++i)
		{
			auto& elementMeta = pStructData->m_StructUniforms.m_Elements[i];
			m_NameToUniformElementMetaID[elementMeta.m_Name] = i;
			m_UniformElementOffsetInStagingBuffer[i] = offset;
			offset += elementMeta.GetFullSize();
		}
		m_StructLocalUniformStagingBuffer.resize(offset);
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
	}

	void D3D2ShaderStruct::SetValueInternal(cacore::NameHash const& name, void const* pValue, uint32_t sizeInBytes, uint32_t elementIndex)
	{
		auto found = m_NameToUniformElementMetaID.find(name);
		if (found != m_NameToUniformElementMetaID.end())
		{
			uint32_t metaID = found->second;
			auto& elementMeta = p_StructData->m_StructUniforms.m_Elements[metaID];
			auto offset = m_UniformElementOffsetInStagingBuffer[metaID];
			CA_ASSERT(elementMeta.m_ElementMemorySize == sizeInBytes, "Size of data does not match size of element in shader struct");
			CA_ASSERT(elementMeta.m_ElementCount > elementIndex, "");
			auto writingOffset = offset + elementMeta.m_Stride * elementIndex;
			CA_ASSERT_BREAK(writingOffset + sizeInBytes <= m_StructLocalUniformStagingBuffer.size(), "Writing Data Exceeds Uniform Staging Buffer");
			
			memcpy(&m_StructLocalUniformStagingBuffer[writingOffset], pValue, sizeInBytes);
		}
		UpdateVersion();
	}
	void D3D2ShaderStruct::SetImageInternal(cacore::NameHash const& name, ImageHandle const& imageHandle, GPUTextureView const& view, uint32_t elementIndex)
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
	void D3D2ShaderStruct::SetBufferInternal(cacore::NameHash const& name, BufferHandle const& bufferHandle, uint32_t elementIndex)
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
	void D3D2ShaderStruct::SetSamplerInternal(cacore::NameHash const& name, TextureSamplerDescriptor const& samplerDesc, uint32_t elementIndex)
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
	void D3D2ShaderStruct::SetStructInternal(cacore::NameHash const& name, castl::shared_ptr<ShaderStruct> const& subStruct, uint32_t elementIndex)
	{
		auto found = m_NameToSubStructs.find(name);
		if (found != m_NameToSubStructs.end())
		{
			auto& structList = found->second;
			CA_ASSERT(structList.size() > elementIndex, "");
			structList[elementIndex] = castl::static_pointer_cast<D3D2ShaderStruct>(subStruct);
		}

		UpdateVersion();
	}
	uint64_t D3D2ShaderStruct::ComputeMaxChildrenVersion() const
	{
		m_MaxChildrenVersion = 0;
		for (auto pair : m_NameToSubStructs)
		{
			for (auto pStruct : pair.second)
			{
				if (pStruct)
				{
					m_MaxChildrenVersion = castl::max(m_MaxChildrenVersion, pStruct->ComputeMaxChildrenVersion());
				}
			}
		}
		return castl::max(m_Version, m_MaxChildrenVersion);
	}
	void D3D2ShaderStruct::UpdateUniformBuffer(uint64_t uniformBufferVersion, void* pOutBuffer, uint32_t bufferSize, uint32_t offset) const
	{
		//Update Elements(Non Struct)
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

		//Update Child Structs
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
	uint64_t D3D2ShaderStruct::GetCBufferSize() const
	{
		return p_StructData->m_StructUniforms.m_MemorySize;
	}
	castl::unordered_map<cacore::NameHash, castl::vector<castl::shared_ptr<D3D2ShaderStruct>>> const& D3D2ShaderStruct::GetSubStructs() const
	{
		return m_NameToSubStructs;
	}

	ETextureAccessType D3D2ShaderStruct::GetTextureAccessType(cacore::NameHash const& textureName) const
	{
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
	ShaderCompilerSlang::EShaderResourceAccess D3D2ShaderStruct::GetBufferRWType(cacore::NameHash const& bufferName) const
	{
		for (auto& buffer : p_StructData->m_Buffers)
		{
			if (buffer.m_Name == bufferName)
			{
				return buffer.m_RWType;
			}
		}
		return ShaderCompilerSlang::EShaderResourceAccess::eUnknown;
	}
	EBufferUsage D3D2ShaderStruct::GetBufferUsage(cacore::NameHash const& bufferName) const
	{
		for (auto& buffer : p_StructData->m_Buffers)
		{
			if (buffer.m_Name == bufferName)
			{
				switch (buffer.m_RWType)
				{
				case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
					return EBufferUsage::eStructuredBuffer;
				case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
				case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
					return EBufferUsage::eStructuredBuffer;
				}
			}
		}
		return EBufferUsage::eMaxBit;
	}
	void D3D2ShaderStruct::UpdateVersion()
	{
		m_Version = GetApp()->GetCurrentFrameVersion();
	}
}