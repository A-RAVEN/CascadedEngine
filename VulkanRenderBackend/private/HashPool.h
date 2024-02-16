#pragma once
#include "VulkanApplicationSubobjectBase.h"
#include <uhash.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAFunctional.h>
#include <DebugUtils.h>
#include <concurrent_unordered_map.h>

namespace graphics_backend
{
	template<typename DescType, typename ValType>
	class HashPool : public VKAppSubObjectBaseNoCopy
	{
	public:

		using map_type = castl::unordered_map<DescType, castl::shared_ptr<ValType>, hash_utils::default_hashAlg>;

		HashPool() = delete;
		HashPool(HashPool const& other) = delete;
		HashPool& operator=(HashPool const&) = delete;
		HashPool(HashPool&& other) = delete;
		HashPool& operator=(HashPool&&) = delete;

		HashPool(CVulkanApplication& application) : VKAppSubObjectBaseNoCopy(application)
		{}

		castl::shared_ptr<ValType> GetOrCreate(DescType const& desc, castl::string const& name = "")
		{
			castl::shared_ptr<ValType> result = nullptr;
			{
				castl::lock_guard<castl::mutex> lockGuard(m_Mutex);

				//CA_ASSERT(name == "", name + " Count Before" + castl::to_ca(std::to_string(m_InternalMap.size())));

				//CA_ASSERT(name == "", name + " Try Found!");
				auto mapEnd = m_InternalMap.end();
				auto it = m_InternalMap.find(desc);
				if (it != mapEnd)
				{
					result = it->second;
					//CA_ASSERT(name == "", name + " Found!");
					//CA_ASSERT(it->first == desc, name + " Found But Not Match!");
				}
				else
				{
					result = castl::make_shared<ValType>(GetVulkanApplication());
					//CA_ASSERT(name == "", name + " Insert Obj");
					it = m_InternalMap.insert(std::make_pair(desc, result)).first;
					//CA_ASSERT(it != m_InternalMap.end(), "Why here");
					result->Create(it->first);
					//CA_ASSERT(name == "", name + " Create Obj");
				}
				//CA_ASSERT(it->first == desc, "Wierd");
				//CA_ASSERT(name == "", name + " Count After" + castl::to_ca(std::to_string(m_InternalMap.size())));
			}
			return result;
		}

		void Foreach(castl::function<void(DescType const&, ValType*)> callbackFunc)
		{
			castl::lock_guard<castl::mutex> lockGuard(m_Mutex);
			for (auto& it : m_InternalMap)
			{
				callbackFunc(it.first, it.second.get());
			};
		}
	private:
		castl::mutex m_Mutex;
		concurrency::concurrent_unordered_map<DescType, castl::shared_ptr<ValType>, hash_utils::default_hashAlg> m_InternalMap;
	};
}