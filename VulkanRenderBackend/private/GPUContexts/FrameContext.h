#pragma once
#include <CASTL/CAAtomic.h>
#include <CASTL/CAVector.h>
#include <CASTL/CADeque.h>
#include <VulkanIncludes.h>
#include <VulkanApplicationSubobjectBase.h>
#include <ResourcePool/FrameBoundResourcePool.h>

namespace graphics_backend
{
	class FrameContext : public VKAppSubObjectBaseNoCopy
	{
	public:
		FrameContext(CVulkanApplication& owner);
		void InitFrameCapacity(uint32_t capacity);
		castl::shared_ptr<FrameBoundResourcePool> GetFrameBoundResourceManager();
		void Release();
	private:
		castl::mutex m_Mutex;
		castl::condition_variable m_ConditionalVal;
		//uint32_t m_FrameIndex;
		castl::vector<FrameBoundResourcePool> m_FrameBoundResourceManagers;
		castl::deque<uint32_t> m_AvailableResourcesManagers;
	};
}