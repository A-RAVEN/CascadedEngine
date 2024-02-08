#pragma once
#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include "ShaderBindingSetHandle.h"
#include "CRenderGraph.h"

namespace graphics_backend
{
	ShaderConstantSetHandle& ShaderConstantSetHandle::SetValue(castl::string const& name, void* pValue)
	{
		p_RenderGraph->GetConstantSetData(GetHandleIndex()).SetValue(name, pValue);
		return *this;
	}

	inline ShaderBindingSetHandle& ShaderBindingSetHandle::SetConstantSet(castl::string const& name, castl::shared_ptr<ShaderConstantSet> const& pConstantSet)
	{
		p_RenderGraph->GetBindingSetData(GetHandleIndex())->SetConstantSet(name, pConstantSet);
		return *this;
	}
	inline ShaderBindingSetHandle& ShaderBindingSetHandle::SetConstantSet(castl::string const& name, ShaderConstantSetHandle const& constantSetHandle)
	{
		p_RenderGraph->GetBindingSetData(GetHandleIndex())->SetConstantSet(name, constantSetHandle);
		return *this;
	}
	inline ShaderBindingSetHandle& ShaderBindingSetHandle::SetTexture(castl::string const& name
		, castl::shared_ptr<GPUTexture> const& pTexture)
	{
		p_RenderGraph->GetBindingSetData(GetHandleIndex())->SetTexture(name, pTexture);
		return *this;
	}
	inline ShaderBindingSetHandle& ShaderBindingSetHandle::SetTexture(castl::string const& name
		, TextureHandle const& textureHandle)
	{
		p_RenderGraph->GetBindingSetData(GetHandleIndex())->SetTexture(name, textureHandle);
		return *this;
	}
	inline ShaderBindingSetHandle& ShaderBindingSetHandle::SetSampler(castl::string const& name
		, castl::shared_ptr<TextureSampler> const& pSampler)
	{
		p_RenderGraph->GetBindingSetData(GetHandleIndex())->SetSampler(name, pSampler);
		return *this;
	}
}