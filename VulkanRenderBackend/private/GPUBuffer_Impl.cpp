#include "pch.h"
#include "GPUBuffer_Impl.h"
#include "VulkanApplication.h"
#include "CVulkanMemoryManager.h"
#include "InterfaceTranslator.h"

namespace graphics_backend
{


	GPUBuffer_Impl::GPUBuffer_Impl(CVulkanApplication& owner) : BaseTickingUpdateResource(owner)
	{
	}

	void GPUBuffer_Impl::Release()
	{
		m_BufferObject.RAIIRelease();
	}

	void GPUBuffer_Impl::Initialize(EBufferUsageFlags usages, uint64_t count, uint64_t stride)
	{
        m_Usages = usages;
		m_Count = count;
		m_Stride = stride;

	}
	bool GPUBuffer_Impl::UploadingDone() const
	{
		return BaseTickingUpdateResource::UploadingDone();
	}
	void GPUBuffer_Impl::ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void const* pData)
	{
		size_t scheduleSize = bufferOffset + dataSize;
		if (scheduleSize > m_ScheduledData.size())
		{
			m_ScheduledData.resize(scheduleSize);
		}
		memcpy(m_ScheduledData.data() + bufferOffset, pData, dataSize);
		MarkDirtyThisFrame();
	}
	void GPUBuffer_Impl::TickUpload()
	{
        auto& memoryManager = GetMemoryManager();
        auto threadContext = GetVulkanApplication().AquireThreadContextPtr();
        auto currentFrame = GetFrameCountContext().GetCurrentFrameID();

        std::atomic_thread_fence(std::memory_order_acquire);

		if (!m_BufferObject.IsRAIIAquired())
		{
			m_BufferObject = memoryManager.AllocateBuffer(
				EMemoryType::GPU
				, EMemoryLifetime::Persistent
				, m_Count * m_Stride
				, EBufferUsageFlagsTranslate(m_Usages) | vk::BufferUsageFlagBits::eTransferDst);
		}

		uint64_t byteSize = std::min(m_Count * m_Stride, m_ScheduledData.size());
        auto tempBuffer = memoryManager.AllocateFrameBoundTransferStagingBuffer(byteSize);
        memcpy(tempBuffer->GetMappedPointer(), m_ScheduledData.data(), byteSize);
		VulkanBarrierCollector barrierCollector{ GetFrameCountContext().GetGraphicsQueueFamily() };
        auto cmdBuffer = threadContext->GetCurrentFramePool().AllocateMiscCommandBuffer("Upload GPU Buffer");
		barrierCollector.PushBufferBarrier(m_BufferObject->GetBuffer(), ResourceUsage::eDontCare, ResourceUsage::eTransferDest);
		barrierCollector.ExecuteBarrier(cmdBuffer);
		barrierCollector.Clear();
		cmdBuffer.copyBuffer(tempBuffer->GetBuffer(), m_BufferObject->GetBuffer(), vk::BufferCopy(0, 0, byteSize));
		barrierCollector.PushBufferBarrier(m_BufferObject->GetBuffer(), ResourceUsage::eTransferDest, ResourceUsage::eVertexAttribute);
		barrierCollector.ExecuteBarrier(cmdBuffer);
		barrierCollector.Clear();
		cmdBuffer.end();
		MarkUploadingDoneThisFrame();
	}
}
