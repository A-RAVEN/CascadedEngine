#pragma once
#include "VulkanApplicationSubobjectBase.h"
#include <uhash.h>
#include <unordered_map>
#include <functional>

namespace graphics_backend
{
	template<typename DescType, typename ValType>
	class HashPool : public VKAppSubObjectBaseNoCopy
	{
	public:

		HashPool() = delete;
		HashPool(HashPool const& other) = delete;
		HashPool& operator=(HashPool const&) = delete;
		HashPool(HashPool&& other) = delete;
		HashPool& operator=(HashPool&&) = delete;

		HashPool(CVulkanApplication& application) : VKAppSubObjectBaseNoCopy(application)
		{}

		castl::shared_ptr<ValType> GetOrCreate(DescType const& desc)
		{
			castl::shared_ptr<ValType> result;
			std::lock_guard<std::mutex> lockGuard(m_Mutex);
			auto it = m_InternalMap.find(desc);
			if (it == m_InternalMap.end())
			{
				result = castl::shared_ptr<ValType>{ new ValType{ GetVulkanApplication() } };
				it = m_InternalMap.insert(std::make_pair(desc, result)).first;
				result->Create(it->first);
			}
			else
			{
				result = it->second;
			}
			return result;
		}

		void Foreach(std::function<void(DescType const&, ValType*)> callbackFunc)
		{
			for (auto& it : m_InternalMap)
			{
				callbackFunc(it.first, it.second.get());
			};
		}
	private:
		std::mutex m_Mutex;
		std::unordered_map<DescType, castl::shared_ptr<ValType>, hash_utils::default_hashAlg> m_InternalMap;
	};
}