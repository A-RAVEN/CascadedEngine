#pragma once
#include <GPUBuffer.h>
#include "RenderBackendSettings.h"
#include "TickUploadingResource.h"
#include "CVulkanBufferObject.h"

namespace graphics_backend
{
	class GPUBuffer_Impl : public BaseTickingUpdateResource, public GPUBuffer
	{
	public:
		GPUBuffer_Impl(CVulkanApplication& owner);
		virtual void Release() override;
		void Initialize(EBufferUsageFlags usages, uint64_t count, uint64_t stride);
		virtual void TickUpload() override;
		// 通过 GPUBuffer 继承
		virtual bool UploadingDone() const override;
		virtual void ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void const* pData) override;
		virtual void Name(castl::string const& name) override { m_Name = name; }
		VulkanBufferHandle const& GetVulkanBufferObject() const { return m_BufferObject; }
	protected:
	private:
		VulkanBufferHandle m_BufferObject;
		EBufferUsageFlags m_Usages;
		uint64_t m_Count = 0;
		uint64_t m_Stride = 0;
		castl::string m_Name;
		castl::vector<char> m_ScheduledData;
	};
}

