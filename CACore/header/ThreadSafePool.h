#pragma once
//#include <mutex>
//#include <deque>
//#include <functional>
#include <CASTL/CAMutex.h>
#include <CASTL/CADeque.h>
#include <CASTL/CAFunctional.h>
#include "DebugUtils.h"

namespace threadsafe_utils
{
	template<typename T>
	class DefaultInitializer
	{
	public:
		void operator()(T* initialize)
		{
			initialize->Initialize();
		}
	};

	template<typename T>
	class DefaultReleaser
	{
	public:
		void operator()(T* release)
		{
			release->Release();
		}
	};

	template<typename T>
	class TThreadSafePointerPool
	{
	public:
		TThreadSafePointerPool() :
			m_Initializer(DefaultInitializer<T>{})
			, m_Releaser(DefaultReleaser<T>{})
		{
		}
		TThreadSafePointerPool(TThreadSafePointerPool const& other) = delete;
		TThreadSafePointerPool& operator=(TThreadSafePointerPool const&) = delete;
		TThreadSafePointerPool(TThreadSafePointerPool&& other) = delete;
		TThreadSafePointerPool& operator=(TThreadSafePointerPool&&) = delete;

		TThreadSafePointerPool(std::function<void(T*)> initializer, std::function<void(T*)> releaser) :
			m_Initializer(initializer)
			, m_Releaser(releaser)
		{
		}
		virtual ~TThreadSafePointerPool()
		{
			CA_ASSERT(IsEmpty(), (std::string{"ThreadSafe Pointer Pool Is Not Released Before Destruct: "} + CA_CLASS_NAME(T)).c_str());
		}

		template<typename...TArgs>
		T* Alloc(TArgs&&...Args)
		{
			std::lock_guard<std::mutex> lockGuard(m_Mutex);
			T* result = nullptr;
			if (m_EmptySpaces.empty())
			{
				m_Pool.emplace_back(std::forward<TArgs>(Args)...);
				result = &m_Pool.back();
			}
			else
			{
				result = m_EmptySpaces.front();
				m_EmptySpaces.pop_front();
			}
			m_Initializer(result);
			return result;
		}

		void Release(T* releaseObj)
		{
			assert(releaseObj != nullptr);
			std::lock_guard<std::mutex> lockGuard(m_Mutex);
			m_EmptySpaces.push_back(releaseObj);
			m_Releaser(releaseObj);
		}

		bool IsEmpty()
		{
			std::lock_guard<std::mutex> lockGuard(m_Mutex);
			return m_EmptySpaces.size() == m_Pool.size();
		}

		uint32_t GetPoolSize() const
		{
			return static_cast<uint32_t>(m_Pool.size());
		}

		uint32_t GetEmptySpaceSize() const
		{
			return static_cast<uint32_t>(m_EmptySpaces.size());
		}
	protected:
		castl::mutex m_Mutex;
		castl::deque<T> m_Pool;
		castl::deque<T*> m_EmptySpaces;
		castl::function<void(T*)> m_Initializer;
		castl::function<void(T*)> m_Releaser;
	};
}