#include "D3D12ShaderStruct.h"

namespace graphics_backend
{
	D3D2ShaderStruct::D3D2ShaderStruct(RenderBackend_D3D12* app, ShaderCompilerSlang::ShaderStructData const* pStructData)
		: D3D12SubobjectBase(app)
		, p_StructData(pStructData)
	{
		m_SelfUniformBuffer.resize(p_StructData->m_StructUniforms.m_MemorySize);
		for (uint32_t i = 0; i < pStructData->m_StructUniforms.m_Elements.size(); ++i)
		{
			auto& elementMeta = pStructData->m_StructUniforms.m_Elements[i];
			m_NameToUniformElementMetaID[elementMeta.m_Name] = i;
		}
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
			CA_ASSERT(elementMeta.m_ElementMemorySize == sizeInBytes, "Size of data does not match size of element in shader struct");
			CA_ASSERT(elementMeta.m_ElementCount > elementIndex, "");
			memcpy(&m_SelfUniformBuffer[elementMeta.m_MemoryOffset + elementMeta.m_Stride * elementIndex]
				, pValue, sizeInBytes);
		}
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
	}
	void D3D2ShaderStruct::SetStructInternal(cacore::NameHash const& name, castl::shared_ptr<ShaderStruct> const& subStruct, uint32_t elementIndex)
	{
		auto found = m_NameToSubStructs.find(name);
		if (found != m_NameToSubStructs.end())
		{
			auto& structList = found->second;
			CA_ASSERT(structList.size() > elementIndex, "");
			structList[elementIndex] = subStruct;
		}

		//Copy Uniform Data from substruct to self
		auto uniformFound = m_NameToUniformElementMetaID.find(name);
		if (uniformFound != m_NameToUniformElementMetaID.end())
		{
			uint32_t metaID = uniformFound->second;
			auto& elementMeta = p_StructData->m_StructUniforms.m_Elements[metaID];
			D3D2ShaderStruct const* pVKSubstruct = static_cast<D3D2ShaderStruct const*>(subStruct.get());
			CA_ASSERT(elementMeta.m_ElementMemorySize == pVKSubstruct->m_SelfUniformBuffer.size(), "Size of data does not match size of element in shader struct");
			CA_ASSERT(elementMeta.m_ElementCount > elementIndex, "");
			memcpy(&m_SelfUniformBuffer[elementMeta.m_MemoryOffset + elementMeta.m_Stride * elementIndex]
				, pVKSubstruct->m_SelfUniformBuffer.data(), pVKSubstruct->m_SelfUniformBuffer.size());
		}
	}
}