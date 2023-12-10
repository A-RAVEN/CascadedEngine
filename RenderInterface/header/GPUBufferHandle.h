#pragma once
#include "Common.h"

namespace graphics_backend
{
	class IGPUBufferInternalData
	{
	public:
		virtual void ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void* pData) = 0;
		virtual void const* GetPointer() const = 0;
		virtual size_t GetSizeInBytes() const = 0;
	};

	class GPUBufferHandle
	{
	public:
		GPUBufferHandle() = default;
		GPUBufferHandle(TIndex handleIndex)
			: m_HandleIndex(handleIndex) {}
		TIndex GetHandleIndex() const { return m_HandleIndex; }
	private:
		TIndex m_HandleIndex = INVALID_INDEX;
	};
}