#include <Platform.h>
#include "FrameContext.h"
namespace graphics_backend
{
	graphics_backend::FrameContext::FrameContext(CVulkanApplication& owner) : VKAppSubObjectBaseNoCopy(owner)
	{
	}

	void FrameContext::InitFrameCapacity(uint32_t capacity)
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		m_FrameBoundResourceManagers.reserve(capacity);
		m_AvailableResourcesManagers.resize(capacity);
		
		for (uint32_t i = 0; i < capacity; i++)
		{
			m_AvailableResourcesManagers[i] = i;
			m_FrameBoundResourceManagers.push_back(castl::move(FrameBoundResourcePool(GetVulkanApplication())));
			m_FrameBoundResourceManagers.back().Initialize();
		}
	}

	castl::shared_ptr<FrameBoundResourcePool> FrameContext::GetFrameBoundResourceManager()
	{
		std::unique_lock<std::mutex> lock(m_Mutex);
		if (m_AvailableResourcesManagers.empty())
		{
			m_ConditionalVal.wait(lock, [this]()
				{
					return !m_AvailableResourcesManagers.empty();
				});
		}
		uint32_t managerIndex = m_AvailableResourcesManagers.front();
		m_AvailableResourcesManagers.pop_front();
		FrameBoundResourcePool* resourcePool = &m_FrameBoundResourceManagers[managerIndex];
		resourcePool->ResetPool();
		castl::shared_ptr<FrameBoundResourcePool> result = castl::shared_ptr<FrameBoundResourcePool>(resourcePool, [this, managerIndex](FrameBoundResourcePool* releasingPool)
			{
				{
					std::unique_lock<std::mutex> lock(m_Mutex);
					m_AvailableResourcesManagers.push_back(managerIndex);
				}
				m_ConditionalVal.notify_one();
			});
		return result;
	}

	void FrameContext::Release()
	{
		for(auto &frameBoundResourceManager : m_FrameBoundResourceManagers)
		{
			frameBoundResourceManager.Release();
		}
	}

}