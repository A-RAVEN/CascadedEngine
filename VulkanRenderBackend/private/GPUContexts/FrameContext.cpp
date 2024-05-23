#include <Platform.h>
#include "FrameContext.h"
namespace graphics_backend
{
	graphics_backend::FrameContext::FrameContext(CVulkanApplication& owner) : VKAppSubObjectBaseNoCopy(owner)
	{
	}

	void FrameContext::InitFrameCapacity(uint32_t capacity)
	{
		m_FrameBoundResourceManagers.reserve(capacity);
		for (uint32_t i = 0; i < capacity; i++)
		{
			m_FrameBoundResourceManagers.push_back(castl::move(FrameBoundResourcePool(GetVulkanApplication())));
			m_FrameBoundResourceManagers.back().Initialize();
		}
	}

	FrameBoundResourcePool* FrameContext::GetFrameBoundResourceManager()
	{
		FrameBoundResourcePool* resourcePool = &m_FrameBoundResourceManagers[m_FrameIndex % m_FrameBoundResourceManagers.size()];
		++m_FrameIndex;
		resourcePool->ResetPool();
		return resourcePool;
	}

	void FrameContext::Release()
	{
		for(auto &frameBoundResourceManager : m_FrameBoundResourceManagers)
		{
			frameBoundResourceManager.Release();
		}
	}

}