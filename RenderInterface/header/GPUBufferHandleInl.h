#pragma once
#include "GPUBufferHandle.h"
#include "CRenderGraph.h"

namespace graphics_backend
{
	inline GPUBufferHandle& GPUBufferHandle::ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void* pData)
	{
		p_RenderGraph->GetGPUBufferInternalData(GetHandleIndex()).ScheduleBufferData(bufferOffset, dataSize, pData);
		return *this;
	}
}