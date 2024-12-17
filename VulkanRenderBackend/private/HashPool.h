#pragma once
#include "VulkanApplicationSubobjectBase.h"
#include <Hasher.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAFunctional.h>
#include <DebugUtils.h>
#include <Utilities/SubobjectTraits.h>
#include <CACore/CASharedDic.h>

namespace graphics_backend
{
	template<typename DescType, typename ValType>
	concept CanCreateWithDesc = requires(DescType desc, ValType val)
	{
		val.Create(desc);
	};

	template<typename DescType, typename ValType>
	requires has_create<ValType, DescType>
	class HashPool : public VKAppSubObjectBaseNoCopy
	{
	public:

		//using map_type = castl::unordered_map<DescType, castl::shared_ptr<ValType>, cacore::hash<DescType>>;

		HashPool() = delete;
		HashPool(HashPool const& other) = delete;
		HashPool& operator=(HashPool const&) = delete;
		HashPool(HashPool&& other) noexcept : VKAppSubObjectBaseNoCopy(other.GetVulkanApplication()), m_InternalDic(castl::move(other.m_InternalDic))
		{
			//castl::lock_guard<castl::mutex> lockGuard(other.m_Mutex);
			//m_InternalMap = castl::move(other.m_InternalMap);
		}
		HashPool& operator=(HashPool&&) = delete;

		HashPool(CVulkanApplication& application) : VKAppSubObjectBaseNoCopy(application)
		{}

		castl::shared_ptr<ValType> GetOrCreate(DescType const& desc, castl::string const& name = "")
		{
			return m_InternalDic.get_or_create(desc, [&](auto inKey)
				{
					castl::shared_ptr<ValType> result = GetVulkanApplication().template NewSubObject_Shared<ValType>(inKey.Get());
					return result;
				});
			//castl::shared_ptr<ValType> result = nullptr;
			//{
			//	castl::lock_guard<castl::mutex> lockGuard(m_Mutex);
			//	auto mapEnd = m_InternalMap.end();
			//	auto it = m_InternalMap.find(desc);
			//	if (it != mapEnd)
			//	{
			//		result = it->second;
			//	}
			//	else
			//	{
			//		auto& VulkanApp = GetVulkanApplication();
			//		result = VulkanApp.template NewSubObject_Shared<ValType>(desc);
			//		it = m_InternalMap.insert(castl::make_pair(desc, result)).first;
			//	}
			//}
			//return result;
		}

		void Foreach(castl::function<void(DescType const&, ValType*)> callbackFunc)
		{
			m_InternalDic.for_each([&](cacore::HashObj<DescType>const& key, castl::shared_ptr<ValType>& value)
				{
					callbackFunc(key.Get(), value.get());
				});

			//castl::lock_guard<castl::mutex> lockGuard(m_Mutex);
			//for (auto& it : m_InternalMap)
			//{
			//	callbackFunc(it.first, it.second.get());
			//};
		}

		void ReleaseAll() requires has_release<ValType>
		{
			m_InternalDic.clear([](cacore::HashObj<DescType> const& key, castl::shared_ptr<ValType>& value)
				{
					value->Release();
				});
			//castl::lock_guard<castl::mutex> lockGuard(m_Mutex);
			//m_InternalMap.clear();
		}

		void Clear()
		{
			m_InternalDic.clear();
		}
	private:
		//castl::mutex m_Mutex;
		//castl::unordered_map<DescType, castl::shared_ptr<ValType>, cacore::hash<DescType>> m_InternalMap;
		castl::shared_dic<DescType, castl::shared_ptr<ValType>> m_InternalDic;

	};
}