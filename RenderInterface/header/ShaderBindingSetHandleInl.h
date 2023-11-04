#pragma once
#include "ShaderBindingSetHandle.h"
#include "CRenderGraph.h"

namespace graphics_backend
{
	inline void ShaderBindingSetHandle::SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet)
	{
		p_RenderGraph->GetBindingSetData(m_SetIndex)->SetConstantSet(name, pConstantSet);
	}
	inline void ShaderBindingSetHandle::SetTexture(std::string const& name
		, std::shared_ptr<GPUTexture> const& pTexture)
	{
		p_RenderGraph->GetBindingSetData(m_SetIndex)->SetTexture(name, pTexture);
	}
	inline void ShaderBindingSetHandle::SetTexture(std::string const& name
		, TextureHandle const& textureHandle)
	{
		p_RenderGraph->GetBindingSetData(m_SetIndex)->SetTexture(name, textureHandle);
	}
	inline void ShaderBindingSetHandle::SetSampler(std::string const& name
		, std::shared_ptr<TextureSampler> const& pSampler)
	{
		p_RenderGraph->GetBindingSetData(m_SetIndex)->SetSampler(name, pSampler);
	}
}