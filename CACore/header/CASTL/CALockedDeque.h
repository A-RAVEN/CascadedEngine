#pragma once
#include "CAMutex.h"
#include "CADeque.h"

namespace castl
{
	template <typename T>
	class locked_deque
	{
	public:
		locked_deque() = default;
		locked_deque(locked_deque const& other)
		{
			lock_guard<mutex> lock(other.m_mutex);
			m_deque = other.m_deque;
		}
		locked_deque& operator=(locked_deque const& other)
		{
			lock_guard<mutex> lock(m_mutex);
			lock_guard<mutex> lock(other.m_mutex);
			m_deque = other.m_deque;
		}
		locked_deque(locked_deque&& other)
		{
			lock_guard<mutex> lock(m_mutex);
			lock_guard<mutex> lock(other.m_mutex);
			m_deque = other.m_deque;
		}
		locked_deque& operator=(locked_deque&& other)
		{
			lock_guard<mutex> lock(other.m_mutex);
			m_deque = other.m_deque;
		}

		void push_back(T const& value)
		{
			lock_guard<mutex> lock(m_mutex);
			m_deque.push_back(value);
		}

		void push_back(T&& value)
		{
			lock_guard<mutex> lock(m_mutex);
			m_deque.push_back(value);
		}

		void push_front(T const& value)
		{
			lock_guard<mutex> lock(m_mutex);
			m_deque.push_front(value);
		}

		void push_front(T&& value)
		{
			lock_guard<mutex> lock(m_mutex);
			m_deque.push_front(value);
		}

		T pop_front()
		{
			lock_guard<mutex> lock(m_mutex);
			T value = m_deque.front();
			m_deque.pop_front();
			return value;
		}

		T pop_back()
		{
			lock_guard<mutex> lock(m_mutex);
			T value = m_deque.back();
			m_deque.pop_back();
			return value;
		}

		bool empty() const
		{
			lock_guard<mutex> lock(m_mutex);
			return m_deque.empty();
		}

		size_t size() const
		{
			lock_guard<mutex> lock(m_mutex);
			return m_deque.size();
		}

		void clear()
		{
			lock_guard<mutex> lock(m_mutex);
			m_deque.clear();
		}

		T& front()
		{
			lock_guard<mutex> lock(m_mutex);
			return m_deque.front();
		}

		T& back()
		{
			lock_guard<mutex> lock(m_mutex);
			return m_deque.back();
		}

		T const& front() const
		{
			lock_guard<mutex> lock(m_mutex);
			return m_deque.front();
		}

		T const& back() const
		{
			lock_guard<mutex> lock(m_mutex);
			return m_deque.back();
		}
	private:
		deque<T> m_deque;
		mutex m_mutex;
	};
}