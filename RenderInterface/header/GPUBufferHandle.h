#pragma once
#include "Common.h"
#include "GPUGraphHandleBase.h"

namespace graphics_backend
{
	class IGPUBufferInternalData
	{
	public:
		virtual void ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void* pData) = 0;
		virtual void const* GetPointer() const = 0;
		virtual size_t GetSizeInBytes() const = 0;
	};

	class GPUBufferHandle : public GPUGraphHandleBase
	{
	public:
		GPUBufferHandle() = default;
		GPUBufferHandle(CRenderGraph* renderGraph, TIndex handleID)
			: GPUGraphHandleBase(renderGraph, handleID)
		{}
		inline GPUBufferHandle& ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void* pData);
	};
}