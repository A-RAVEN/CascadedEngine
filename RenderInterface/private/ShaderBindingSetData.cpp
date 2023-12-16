#include "pch.h"
#include "ShaderBindingSetData.h"
#include <header/CRenderGraph.h>

namespace graphics_backend
{
	template<typename T>
	void TrySetValue(std::unordered_map<std::string, T>& inoutMap, std::string const& name, T const& inValue)
	{
		auto found = inoutMap.find(name);
		if (found != inoutMap.end())
		{
			found->second = inValue;
		}
		else
		{
			inoutMap.insert(std::make_pair(name, inValue));
		}
	}

	void ShaderBindingSetData_Internal::SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet)
	{
		TrySetValue(m_ExternalConstantSets, name, pConstantSet);
	}
	void ShaderBindingSetData_Internal::SetConstantSet(std::string const& name, ShaderConstantSetHandle const& constantSetHandle)
	{
		TrySetValue(m_InternalConstantSets, name, constantSetHandle);
	}
	void ShaderBindingSetData_Internal::SetTexture(std::string const& name, std::shared_ptr<GPUTexture> const& pTexture)
	{
		TrySetValue(m_ExternalTextures, name, pTexture);
	}
	void ShaderBindingSetData_Internal::SetTexture(std::string const& name, TextureHandle const& textureHandle)
	{
		TrySetValue(m_InternalTextures, name, textureHandle);
	}
	void ShaderBindingSetData_Internal::SetGPUBuffer(std::string const& name, GPUBufferHandle const& bufferHandle)
	{
		TrySetValue(m_InternalBuffers, name, bufferHandle);
	}
	void ShaderBindingSetData_Internal::SetSampler(std::string const& name, std::shared_ptr<TextureSampler> const& pSampler)
	{
		TrySetValue(m_ExternalSamplers, name, pSampler);
	}
}

