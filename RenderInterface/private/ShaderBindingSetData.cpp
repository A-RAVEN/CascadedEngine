#include "pch.h"
#include "ShaderBindingSetData.h"
#include <header/CRenderGraph.h>

namespace graphics_backend
{
	void ShaderBindingSetData_Internal::SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet)
	{
		auto found = m_ExternalConstantSets.find(name);
		if (found != m_ExternalConstantSets.end())
		{
			found->second = pConstantSet;
		}
		else
		{
			m_ExternalConstantSets.insert(std::make_pair(name, pConstantSet));
		}
	}
	void ShaderBindingSetData_Internal::SetTexture(std::string const& name, std::shared_ptr<GPUTexture> const& pTexture)
	{
		auto found = m_ExternalTextures.find(name);
		if (found != m_ExternalTextures.end())
		{
			found->second = pTexture;
		}
		else
		{
			m_ExternalTextures.insert(std::make_pair(name, pTexture));
		}
	}

	void ShaderBindingSetData_Internal::SetTexture(std::string const& name, TextureHandle const& textureHandle)
	{
		auto found = m_InternalTextures.find(name);
		if (found != m_InternalTextures.end())
		{
			found->second = textureHandle;
		}
		else
		{
			m_InternalTextures.insert(std::make_pair(name, textureHandle));
		}
	}

	void ShaderBindingSetData_Internal::SetGPUBuffer(std::string const& name, GPUBufferHandle const& bufferHandle)
	{
		auto found = m_InternalBuffers.find(name);
		if (found != m_InternalBuffers.end())
		{
			found->second = bufferHandle;
		}
		else
		{
			m_InternalBuffers.insert(std::make_pair(name, bufferHandle));
		}
	}

	void ShaderBindingSetData_Internal::SetSampler(std::string const& name, std::shared_ptr<TextureSampler> const& pSampler)
	{
		auto found = m_ExternalSamplers.find(name);
		if (found != m_ExternalSamplers.end())
		{
			found->second = pSampler;
		}
		else
		{
			m_ExternalSamplers.insert(std::make_pair(name, pSampler));
		}
	}
}

