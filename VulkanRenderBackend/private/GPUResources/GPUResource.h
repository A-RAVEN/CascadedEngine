#pragma once
#include <Platform.h>
#include <ResourceUsageInfo.h>
#include <VulkanIncludes.h>

namespace graphics_backend
{
	class GPUResource
	{
	public:
		void SetUsage(ResourceUsageFlags usage) { m_ResourceUsage = usage; }
		ResourceUsageFlags GetUsage() const { return m_ResourceUsage; }

		void SetQueueFamily(uint32_t queueFamily) { m_QueueFamily = queueFamily; }
		uint32_t GetQueueFamily() const {  return m_QueueFamily; }
	private:
		ResourceUsageFlags m_ResourceUsage = ResourceUsage::eDontCare;
		uint32_t m_QueueFamily = castl::numeric_limits<uint32_t>::max();
	};
}