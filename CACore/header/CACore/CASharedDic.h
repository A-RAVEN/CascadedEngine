#pragma once
#include <Hasher.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAFunctional.h>

namespace castl
{
	template<typename TKey, typename TValue>
	class shared_dic
	{
	public:
		shared_dic() = default;
		shared_dic(shared_dic&& other) noexcept
		{
			castl::unique_lock write_lock(other.m_SharedMutex);
			m_Map = castl::move(other.m_Map);
		}
		TValue& get_or_create(cacore::HashObj<TKey> const& inKey, castl::function<TValue(cacore::HashObj<TKey> const&)> createFunctor)
		{
			{
				castl::shared_lock lock(m_SharedMutex);
				auto found = m_Map.find(inKey);
				if (found != m_Map.end())
				{
					return found->second;
				}
			}
			{
				castl::unique_lock write_lock(m_SharedMutex);
				auto found = m_Map.find(inKey);
				if (found == m_Map.end())
				{
					found = m_Map.insert(castl::make_pair(inKey, castl::move(createFunctor(inKey)))).first;
				}
				return found->second;
			}
		}
		void for_each_const(castl::function<void(cacore::HashObj<TKey> const&, TValue const&)> callback) const
		{
			castl::shared_lock lock(m_SharedMutex);
			for (auto pair : m_Map)
			{
				callback(pair.first, pair.second);
			}
		}
		//void for_each_const(castl::function<void(TKey const&, TValue const&)> callback) const
		//{
		//	for_each_const([&](cacore::HashObj<TKey> const& key, TValue const& value)
		//		{
		//			callback(key.Get(), value);
		//		});
		//}
		void for_each(castl::function<void(cacore::HashObj<TKey> const&, TValue&)> callback)
		{
			castl::unique_lock write_lock(m_SharedMutex);
			for (auto itr = m_Map.begin(); itr != m_Map.end(); ++itr)
			{
				callback(itr->first, itr->second);
			}
		}
		//void for_each(castl::function<void(TKey const&, TValue&)> callback)
		//{
		//	for_each([&](cacore::HashObj<TKey> const& key, TValue& value)
		//		{
		//			callback(key.Get(), value);
		//		});
		//}
		void clear()
		{
			castl::unique_lock write_lock(m_SharedMutex);
			m_Map.clear();
		}
		void clear(castl::function<void(cacore::HashObj<TKey> const&, TValue&)> callback)
		{
			castl::unique_lock write_lock(m_SharedMutex);
			for (auto itr = m_Map.begin(); itr != m_Map.end(); ++itr)
			{
				callback(itr->first.Get(), itr->second);
			}
			m_Map.clear();
		}
		//void clear(castl::function<void(TKey const&, TValue&)> callback)
		//{
		//	clear([](cacore::HashObj<TKey> const& key, TValue& value)
		//		{
		//			callback(key.Get(), value);
		//		});
		//}
	private:
		castl::shared_mutex m_SharedMutex;
		castl::unordered_map<cacore::HashObj<TKey>, TValue, cacore::hash<cacore::HashObj<TKey>>> m_Map;
	};
}
