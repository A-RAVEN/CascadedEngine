#pragma once
#include "RenderBackendSettings.h"
#include "IUploadingResource.h"
#include "CVulkanBufferObject.h"
#include <RenderInterface/header/GPUBuffer.h>

namespace graphics_backend
{
	class GPUBuffer_Impl : public BaseUploadingResource, public GPUBuffer
	{
	public:
		GPUBuffer_Impl(CVulkanApplication& owner);
		virtual void Release() override;
		void Initialize(EBufferUsageFlags usages, uint64_t count, uint64_t stride);
		void UploadAsync() override;
		virtual bool UploadingDone() const override;
		// 通过 GPUBuffer 继承
		virtual void ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void const* pData) override;
		VulkanBufferHandle const& GetVulkanBufferObject() const { return m_BufferObject; }
	protected:
		virtual void DoUpload() override;
	private:
		VulkanBufferHandle m_BufferObject;
		EBufferUsageFlags m_Usages;
		uint64_t m_Count = 0;
		uint64_t m_Stride = 0;
		std::vector<char> m_ScheduledData;
	};
}

