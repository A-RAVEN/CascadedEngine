#pragma once
#include <Hasher.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAFunctional.h>
#include <DebugUtils.h>

namespace castl
{
	template<typename TKey, typename TValue>
	class shared_dic
	{
	public:
		static_assert(cacore::equal_test<TKey>, "TKey Cannot Equal");
		static_assert(cacore::hashable<TKey>, "TKey Not Hashable");

		using map_type = castl::unordered_map<TKey, TValue, cacore::hash<TKey>>;
		using map_iterator = map_type::iterator;

		shared_dic() = default;
		shared_dic(shared_dic&& other) noexcept
		{
			castl::unique_lock write_lock(other.m_SharedMutex);
			m_Map = castl::move(other.m_Map);
		}

		TValue const* try_get(TKey const& inKey) const
		{
			castl::shared_lock lock(m_SharedMutex);
			auto found = m_Map.find(inKey);
			if (found != m_Map.end())
			{
				return &found->second;
			}
			return nullptr;
		}

		TValue* try_get(TKey const& inKey)
		{
			castl::shared_lock lock(m_SharedMutex);
			auto found = m_Map.find(inKey);
			if (found != m_Map.end())
			{
				return &found->second;
			}
			return nullptr;
		}

		bool try_erase(TKey const& inKey)
		{
			castl::shared_lock lock(m_SharedMutex);
			auto found = m_Map.find(inKey);
			if (found != m_Map.end())
			{
				m_Map.erase(found);
				return true;
			}
			return false;
		}

		map_type::iterator get_or_create(TKey const& inKey, castl::function<TValue()> createFunctor)
		{
			{
				castl::shared_lock lock(m_SharedMutex);
				auto found = m_Map.find(inKey);
				if (found != m_Map.end())
				{
					return found;
				}
			}
			{
				castl::unique_lock write_lock(m_SharedMutex);
				auto found = m_Map.find(inKey);
				if (found == m_Map.end())
				{
					found = m_Map.insert(castl::make_pair(inKey, castl::forward<TValue>(createFunctor()))).first;
				}
				return found;
			}
		}

		map_type::iterator get_or_create(TKey const& inKey, castl::function<TValue(TKey const&)> createFunctor)
		{
			{
				castl::shared_lock lock(m_SharedMutex);
				auto found = m_Map.find(inKey);
				if (found != m_Map.end())
				{
					return found;
				}
			}
			{
				castl::unique_lock write_lock(m_SharedMutex);
				auto found = m_Map.find(inKey);
				if (found == m_Map.end())
				{
					found = m_Map.insert(castl::make_pair(inKey, castl::forward<TValue>(createFunctor(inKey)))).first;
				}
				return found;
			}
		}

		map_iterator get_or_create(TKey const& inKey
			, castl::function<TValue(TKey const&)> createFunctor
			, castl::function<void(map_iterator&)> initializeFunctor)
		{
			{
				castl::shared_lock lock(m_SharedMutex);
				auto found = m_Map.find(inKey);
				if (found != m_Map.end())
				{
					return found;
				}
			}
			{
				castl::unique_lock write_lock(m_SharedMutex);
				auto found = m_Map.find(inKey);
				if (found == m_Map.end())
				{
					found = m_Map.insert(castl::make_pair(inKey, castl::move(createFunctor(inKey)))).first;
					initializeFunctor(found);
				}
				return found;
			}
		}

		map_iterator begin()
		{
			castl::shared_lock lock(m_SharedMutex);
			return m_Map.begin();
		}

		void for_each_const(castl::function<void(TKey const&, TValue const&)> callback) const
		{
			castl::shared_lock lock(m_SharedMutex);
			for (auto pair : m_Map)
			{
				callback(pair.first, pair.second);
			}
		}

		void for_each(castl::function<void(TKey const&, TValue&)> callback)
		{
			castl::unique_lock write_lock(m_SharedMutex);
			for (auto itr = m_Map.begin(); itr != m_Map.end(); ++itr)
			{
				callback(itr->first, itr->second);
			}
		}
		void clear()
		{
			castl::unique_lock write_lock(m_SharedMutex);
			m_Map.clear();
		}
		void clear(castl::function<void(TKey const&, TValue&)> callback)
		{
			castl::unique_lock write_lock(m_SharedMutex);
			for (auto itr = m_Map.begin(); itr != m_Map.end(); ++itr)
			{
				callback(itr->first, itr->second);
			}
			m_Map.clear();
		}

		size_t size() const
		{
			castl::shared_lock lock(m_SharedMutex);
			return m_Map.size();
		}

		bool empty() const
		{
			castl::shared_lock lock(m_SharedMutex);
			return m_Map.empty();
		}

	private:
		mutable castl::shared_mutex m_SharedMutex;
		map_type m_Map;
	};
}
