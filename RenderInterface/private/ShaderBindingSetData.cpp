#include "pch.h"
#include <CRenderGraph.h>
#include "ShaderBindingSetData.h"

namespace graphics_backend
{
	template<typename T>
	void TrySetValue(castl::unordered_map<castl::string, T>& inoutMap, castl::string const& name, T const& inValue)
	{
		auto found = inoutMap.find(name);
		if (found != inoutMap.end())
		{
			found->second = inValue;
		}
		else
		{
			inoutMap.insert(castl::make_pair(name, inValue));
		}
	}

	void ShaderBindingSetData_Internal::SetConstantSet(castl::string const& name, castl::shared_ptr<ShaderConstantSet> const& pConstantSet)
	{
		TrySetValue(m_ExternalConstantSets, name, pConstantSet);
	}
	void ShaderBindingSetData_Internal::SetConstantSet(castl::string const& name, ShaderConstantSetHandle const& constantSetHandle)
	{
		TrySetValue(m_InternalConstantSets, name, constantSetHandle);
	}
	void ShaderBindingSetData_Internal::SetTexture(castl::string const& name, castl::shared_ptr<GPUTexture> const& pTexture)
	{
		TrySetValue(m_ExternalTextures, name, pTexture);
	}
	void ShaderBindingSetData_Internal::SetTexture(castl::string const& name, TextureHandle const& textureHandle)
	{
		TrySetValue(m_InternalTextures, name, textureHandle);
	}
	void ShaderBindingSetData_Internal::SetGPUBuffer(castl::string const& name, GPUBufferHandle const& bufferHandle)
	{
		TrySetValue(m_InternalBuffers, name, bufferHandle);
	}
	void ShaderBindingSetData_Internal::SetSampler(castl::string const& name, castl::shared_ptr<TextureSampler> const& pSampler)
	{
		TrySetValue(m_ExternalSamplers, name, pSampler);
	}
}

