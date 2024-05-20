#pragma once
#include <CASTL/CAAtomic.h>
#include <CASTL/CAVector.h>
#include <VulkanIncludes.h>
#include <VulkanApplicationSubobjectBase.h>

namespace graphics_backend
{
	class FrameContext : public VKAppSubObjectBaseNoCopy
	{
	public:
		FrameContext(CVulkanApplication& owner);
		void InitFrameCapacity(uint32_t capacity);
		void Release();
	private:
		castl::atomic<uint32_t> m_FrameIndex;
		castl::vector<vk::Fence> m_InFlightFences;
	};
}