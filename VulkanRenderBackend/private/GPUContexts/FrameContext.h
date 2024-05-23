#pragma once
#include <CASTL/CAAtomic.h>
#include <CASTL/CAVector.h>
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
		FrameBoundResourcePool* GetFrameBoundResourceManager();
		void Release();
	private:
		uint32_t m_FrameIndex;
		castl::vector<FrameBoundResourcePool> m_FrameBoundResourceManagers;
	};
}