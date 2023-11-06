#pragma once
#include "ShaderBindingSetHandle.h"
#include "CRenderGraph.h"

namespace graphics_backend
{
	inline ShaderBindingSetHandle& ShaderBindingSetHandle::SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet)
	{
		p_RenderGraph->GetBindingSetData(m_SetIndex)->SetConstantSet(name, pConstantSet);
		return *this;
	}
	inline ShaderBindingSetHandle& ShaderBindingSetHandle::SetTexture(std::string const& name
		, std::shared_ptr<GPUTexture> const& pTexture)
	{
		p_RenderGraph->GetBindingSetData(m_SetIndex)->SetTexture(name, pTexture);
		return *this;
	}
	inline ShaderBindingSetHandle& ShaderBindingSetHandle::SetTexture(std::string const& name
		, TextureHandle const& textureHandle)
	{
		p_RenderGraph->GetBindingSetData(m_SetIndex)->SetTexture(name, textureHandle);
		return *this;
	}
	inline ShaderBindingSetHandle& ShaderBindingSetHandle::SetSampler(std::string const& name
		, std::shared_ptr<TextureSampler> const& pSampler)
	{
		p_RenderGraph->GetBindingSetData(m_SetIndex)->SetSampler(name, pSampler);
		return *this;
	}
}