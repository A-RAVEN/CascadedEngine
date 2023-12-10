#include "GPUBufferData.h"
#include "GPUBufferData.h"
#include "GPUBufferData.h"
#include "GPUBufferData.h"
#include "GPUBufferData.h"

namespace graphics_backend
{
	void GPUBufferData_Internal::ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void* pData)
	{
		size_t scheduleSize = bufferOffset + dataSize;
		if (scheduleSize > m_ScheduledData.size())
		{
			m_ScheduledData.resize(scheduleSize);
		}
		memcpy(m_ScheduledData.data() + bufferOffset, pData, dataSize);
	}
	void const* graphics_backend::GPUBufferData_Internal::GetPointer() const
	{
		return &m_ScheduledData[0];
	}
	size_t graphics_backend::GPUBufferData_Internal::GetSizeInBytes() const
	{
		return m_ScheduledData.size();
	}
}