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
			CA_ASSERT(IsEmpty(), std::string{"Vulkan Application Pointer Pool Is Not Released Before Destruct: "} + CA_CLASS_NAME(T));
		}

		template<typename...TArgs>
		T* Alloc(TArgs&&...Args)
		{
			std::lock_guard<std::mutex> lockGuard(m_Mutex);
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
			result->Initialize(std::forward<TArgs>(Args)...);
			return result;
		}

		void Release(T* releaseObj)
		{
			CA_ASSERT(releaseObj != nullptr, std::string{"Try Release nullptr: "} + CA_CLASS_NAME(T));
			std::lock_guard<std::mutex> lockGuard(m_Mutex);
			releaseObj->Release();
			m_EmptySpaces.push_back(releaseObj);
		}

		void ReleaseAll()
		{
			std::lock_guard<std::mutex> lockGuard(m_Mutex);

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
			return castl::shared_ptr<T>(Alloc(std::forward<TArgs>(Args)...), [this](T* releaseObj) { Release(releaseObj); });
		}

		bool IsEmpty()
		{
			std::lock_guard<std::mutex> lockGuard(m_Mutex);
			return m_EmptySpaces.size() == m_Pool.size();
		}
	private:
		CVulkanApplication& m_Owner;
		std::mutex m_Mutex;
		castl::deque<T> m_Pool;
		castl::deque<T*> m_EmptySpaces;
	};

	template<typename T>
	class TFrameboundReleaser
	{
	public:
		TFrameboundReleaser(std::function<void(castl::deque<T> const&)> const& releasingFunc) :
			m_ReleaseFunc(releasingFunc)
		{
		}
		//Called When Release
		void ScheduleRelease(FrameType frameType, T&& releaseObj)
		{
			std::lock_guard<std::mutex> lockGuard(m_Mutex);
			if ((!m_WaitForRelease.empty()) && m_WaitForRelease.back().first == frameType)
			{
				m_WaitForRelease.back().second.push_back(releaseObj);
				return;
			}
			CA_ASSERT((m_WaitForRelease.empty() || m_WaitForRelease.back().first < frameType), "Inconsistent FrameIndex");
			castl::deque<T> newVec;
			newVec.emplace_back(std::move(releaseObj));
			m_WaitForRelease.emplace_back(std::make_pair(frameType, std::move(newVec)));
		}
		//Called When Frame Done
		void ReleaseFrame(FrameType doneFrame)
		{
			std::lock_guard<std::mutex> lockGuard(m_Mutex);
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
			std::lock_guard<std::mutex> lockGuard(m_Mutex);
			CA_ASSERT(m_ReleaseFunc != nullptr, "Framebound Releaser Has NULL Release Func!");
			for (auto& itrPair : m_WaitForRelease)
			{
				m_ReleaseFunc(itrPair.second);
			}
			m_WaitForRelease.clear();
		}
	private:
		std::mutex m_Mutex;
		std::function<void(castl::deque<T> const&)> m_ReleaseFunc;
		castl::deque<std::pair<FrameType, castl::deque<T>>> m_WaitForRelease;
	};
}