#pragma once
#include "header/GPUBufferHandle.h"

namespace graphics_backend
{
	class GPUBufferData_Internal : public IGPUBufferInternalData
	{
	public:
		virtual void ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void* pData) override;
		void const* GetPointer() const override;
		virtual size_t GetSizeInBytes() const override;
	private:
		std::vector<char> m_ScheduledData;
	};
}