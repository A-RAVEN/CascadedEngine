#pragma once
#include <mutex>
#include <CASTL/CADeque.h>
#include <CASTL/CAFunctional.h>
#include <CASTL/CAUnorderedSet.h>
#include <DebugUtils.h>
#include "RenderBackendSettings.h"
#include "VulkanApplicationSubobjectBase.h"
namespace graphics_backend
{
	template<typename T>
	class TVulkanApplicationPool
	{
	public:
		TVulkanApplicationPool() = delete;
		TVulkanApplicationPool(TVulkanApplicationPool const& other) = delete;
		TVulkanApplicationPool& operator=(TVulkanApplicationPool const&) = delete;
		TVulkanApplicationPool(TVulkanApplicationPool&& other) = delete;
		TVulkanApplicationPool & operator=(TVulkanApplicationPool&&) = delete;

		TVulkanApplicationPool(CVulkanApplication& owner) :
			m_Owner(owner)
		{
		}

		virtual ~TVulkanApplicationPool()
		{
			CA_ASSERT(IsEmpty(), castl::string{"Vulkan Application Pointer Pool Is Not Released Before Destruct: "} + CA_CLASS_NAME(T));
		}

		template<typename...TArgs>
		T* Alloc(TArgs&&...Args)
		{
			castl::lock_guard<castl::mutex> lockGuard(m_Mutex);
			T* result = nullptr;
			if (m_EmptySpaces.empty())
			{
				m_Pool.emplace_back(m_Owner);
				result = &m_Pool.back();
			}
			else
			{
				result = m_EmptySpaces.front();
				m_EmptySpaces.pop_front();
			}
			result->Initialize(castl::forward<TArgs>(Args)...);
			return result;
		}

		void Release(T* releaseObj)
		{
			CA_ASSERT(releaseObj != nullptr, castl::string{"Try Release nullptr: "} + CA_CLASS_NAME(T));
			castl::lock_guard<castl::mutex> lockGuard(m_Mutex);
			releaseObj->Release();
			m_EmptySpaces.push_back(releaseObj);
		}

		void ReleaseAll()
		{
			castl::lock_guard<castl::mutex> lockGuard(m_Mutex);

			castl::unordered_set<T*> emptySet;
			for (T* emptyPtr : m_EmptySpaces)
			{
				emptySet.insert(emptyPtr);
			}

			for (size_t i = 0; i < m_Pool.size(); ++i)
			{
				if (emptySet.find(&m_Pool[i]) == emptySet.end())
				{
					m_Pool[i].Release();
				}
			}
			m_Pool.clear();
			emptySet.clear();
			m_EmptySpaces.clear();
		}

		template<typename...TArgs>
		castl::shared_ptr<T> AllocShared(TArgs&&...Args)
		{
			return castl::shared_ptr<T>(Alloc(castl::forward<TArgs>(Args)...), [this](T* releaseObj) { Release(releaseObj); });
		}

		bool IsEmpty()
		{
			castl::lock_guard<castl::mutex> lockGuard(m_Mutex);
			return m_EmptySpaces.size() == m_Pool.size();
		}
	private:
		CVulkanApplication& m_Owner;
		castl::mutex m_Mutex;
		castl::deque<T> m_Pool;
		castl::deque<T*> m_EmptySpaces;
	};

	template<typename T>
	class TFrameboundReleaser
	{
	public:
		TFrameboundReleaser(castl::function<void(castl::deque<T> const&)> const& releasingFunc) :
			m_ReleaseFunc(releasingFunc)
		{
		}
		//Called When Release
		void ScheduleRelease(FrameType frameType, T&& releaseObj)
		{
			castl::lock_guard<castl::mutex> lockGuard(m_Mutex);
			if ((!m_WaitForRelease.empty()) && m_WaitForRelease.back().first == frameType)
			{
				m_WaitForRelease.back().second.push_back(releaseObj);
				return;
			}
			CA_ASSERT((m_WaitForRelease.empty() || m_WaitForRelease.back().first < frameType), "Inconsistent FrameIndex");
			castl::deque<T> newVec;
			newVec.emplace_back(castl::move(releaseObj));
			m_WaitForRelease.emplace_back(castl::make_pair(frameType, castl::move(newVec)));
		}
		//Called When Frame Done
		void ReleaseFrame(FrameType doneFrame)
		{
			castl::lock_guard<castl::mutex> lockGuard(m_Mutex);
			CA_ASSERT(m_ReleaseFunc != nullptr, "Framebound Releaser Has NULL Release Func!");
			if(m_WaitForRelease.empty())
				return;
			while ((!m_WaitForRelease.empty()) && m_WaitForRelease.front().first <= doneFrame)
			{
				m_ReleaseFunc(m_WaitForRelease.front().second);
				m_WaitForRelease.pop_front();
			};
		}
		void ReleaseAll()
		{
			castl::lock_guard<castl::mutex> lockGuard(m_Mutex);
			CA_ASSERT(m_ReleaseFunc != nullptr, "Framebound Releaser Has NULL Release Func!");
			for (auto& itrPair : m_WaitForRelease)
			{
				m_ReleaseFunc(itrPair.second);
			}
			m_WaitForRelease.clear();
		}
	private:
		castl::mutex m_Mutex;
		castl::function<void(castl::deque<T> const&)> m_ReleaseFunc;
		castl::deque<castl::pair<FrameType, castl::deque<T>>> m_WaitForRelease;
	};
}