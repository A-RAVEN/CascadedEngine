#pragma once
#include "VulkanApplicationSubobjectBase.h"
#include <Hasher.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAFunctional.h>
#include <DebugUtils.h>
#include <concurrent_unordered_map.h>

namespace graphics_backend
{
	template<typename DescType, typename ValType>
	concept CanCreateWithDesc = requires(DescType desc, ValType val)
	{
		val.Create(desc);
	};

	template<typename DescType, typename ValType>
	requires CanCreateWithDesc<DescType, ValType>
	class HashPool : public VKAppSubObjectBaseNoCopy
	{
	public:

		using map_type = castl::unordered_map<DescType, castl::shared_ptr<ValType>, cacore::hash<DescType>>;

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
				auto mapEnd = m_InternalMap.end();
				auto it = m_InternalMap.find(desc);
				if (it != mapEnd)
				{
					result = it->second;
				}
				else
				{
					result = GetVulkanApplication().NewSubObject_Shared<ValType>(desc);
					it = m_InternalMap.insert(castl::make_pair(desc, result)).first;
				}
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
		castl::unordered_map<DescType, castl::shared_ptr<ValType>, cacore::hash<DescType>> m_InternalMap;
	};
}