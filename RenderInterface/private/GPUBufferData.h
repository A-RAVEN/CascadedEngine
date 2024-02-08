#pragma once
#include <CASTL/CAVector.h>
#include <GPUBufferHandle.h>

namespace graphics_backend
{
	class GPUBufferData_Internal : public IGPUBufferInternalData
	{
	public:
		virtual void ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void* pData) override;
		void const* GetPointer() const override;
		virtual size_t GetSizeInBytes() const override;
	private:
		castl::vector<uint8_t> m_ScheduledData;
	};
}