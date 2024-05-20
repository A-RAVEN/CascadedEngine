#include "FrameContext.h"
namespace graphics_backend
{
	graphics_backend::FrameContext::FrameContext(CVulkanApplication& owner) : VKAppSubObjectBaseNoCopy(owner)
	{
	}

	void FrameContext::InitFrameCapacity(uint32_t capacity)
	{
		vk::FenceCreateInfo createInfo{ vk::FenceCreateFlagBits::eSignaled };
		m_InFlightFences.resize(capacity);
		for (auto& fence : m_InFlightFences)
			fence = GetDevice().createFence(createInfo);
	}

	void FrameContext::Release()
	{
		for (auto& fence : m_InFlightFences)
			GetDevice().destroyFence(fence);
	}

}