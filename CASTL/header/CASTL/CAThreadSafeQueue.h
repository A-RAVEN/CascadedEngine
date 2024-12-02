#pragma once
#include "CADeque.h"
#include "CAMutex.h"
#include "CASharedPtr.h"
namespace castl
{
	template<typename T>
	class threadsafe_queue
	{
	public:
		void enqueue(T const& value)
		{
			castl::unique_lock<castl::mutex> lock(m_Mutex);
			m_Queue.push_back(value);
		}
		bool try_deque(T& out_value)
		{
			castl::unique_lock<castl::mutex> lock(m_Mutex);
			if (m_Queue.empty())
			{
				return false;
			}
			out_value = m_Queue.front();
			m_Queue.pop_front();
			return true;
		}
	private:
		castl::mutex m_Mutex;
		castl::deque<T> m_Queue;
	};
}