#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAFunctional.h>
#include <DebugUtils.h>

namespace castl
{
	template<typename TValue>
	class shared_list
	{
	public:
		using vec_type = castl::vector<TValue>;
		using vec_iterator = vec_type::iterator;
		shared_list() = default;
		shared_list(shared_list&& other) noexcept
		{
			castl::unique_lock write_lock(other.m_SharedMutex);
			m_Vec = castl::move(other.m_Vec);
		}
		shared_list(shared_list const& other) noexcept
		{
			castl::unique_lock write_lock(other.m_SharedMutex);
			m_Vec = vec_type(other.m_Vec.begin(), other.m_Vec.end());
		}
		size_t push_back(TValue const& other)
		{
			castl::unique_lock lock(m_SharedMutex);
			m_Vec.push_back(other);
			return m_Vec.size();
		}
		bool push_back_if(TValue const& value, castl::function<bool()> callback)
		{
			castl::unique_lock lock(m_SharedMutex);
			if (callback())
			{
				m_Vec.push_back(value);
				return true;
			}
			return false;
		}
		void pop_back()
		{
			castl::unique_lock lock(m_SharedMutex);
			m_Vec.pop_back();
		}

		void for_each_const(castl::function<void(TValue const&)> callback) const
		{
			castl::shared_lock lock(m_SharedMutex);
			for (TValue const& val : m_Vec)
			{
				callback(val);
			}
		}

		void for_each(castl::function<void(TValue&)> callback)
		{
			castl::unique_lock lock(m_SharedMutex);
			for (TValue& val : m_Vec)
			{
				callback(val);
			}
		}
		void clear(castl::function<void(TValue&)> callback)
		{
			castl::unique_lock write_lock(m_SharedMutex);
			for (TValue& val : m_Vec)
			{
				callback(val);
			}
			m_Vec.clear();
		}
	private:
		mutable castl::shared_mutex m_SharedMutex;
		vec_type m_Vec;
	};

}