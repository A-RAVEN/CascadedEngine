#pragma once
#include "Common.h"

namespace graphics_backend
{
	class GPUBufferHandle
	{
	public:
		TIndex GetHandleIndex() const { return m_HandleIndex; }
		virtual void ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void* pData) = 0;
	private:
		TIndex m_HandleIndex;
	};
}