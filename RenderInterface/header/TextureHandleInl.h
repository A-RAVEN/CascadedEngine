#pragma once
#include "TextureHandle.h"
#include "CRenderGraph.h"

namespace graphics_backend
{
	inline TextureHandle& TextureHandle::ScheduleTextureData(uint64_t textureDataOffset, uint64_t dataSize, void* pData)
	{
		p_RenderGraph->GetGPUTextureInternalData(GetHandleIndex()).ScheduleTextureData(textureDataOffset, dataSize, pData);
		return *this;
	}

	inline GPUTextureDescriptor const& TextureHandle::GetTextureDesc() const
	{
		return p_RenderGraph->GetTextureDescriptor(*this);
	}
}