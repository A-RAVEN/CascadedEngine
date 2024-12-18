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
		}
		HashPool& operator=(HashPool&&) = delete;

		HashPool(CVulkanApplication& application) : VKAppSubObjectBaseNoCopy(application)
		{}

		castl::shared_ptr<ValType> GetOrCreate(DescType const& desc, castl::string const& name = "")
		{
			auto resultPair = m_InternalDic.get_or_create(desc, [&](auto inKey)
				{
					castl::shared_ptr<ValType> result = GetVulkanApplication().template NewSubObject_Shared<ValType>();
					return result;
				});
			CVulkanApplication::InitObj(resultPair->second.get(), resultPair->first.Get());
			return resultPair->second;
		}

		void Foreach(castl::function<void(DescType const&, ValType*)> callbackFunc)
		{
			m_InternalDic.for_each([&](cacore::HashObj<DescType>const& key, castl::shared_ptr<ValType>& value)
				{
					callbackFunc(key.Get(), value.get());
				});
		}

		void ReleaseAll() requires has_release<ValType>
		{
			m_InternalDic.clear([](cacore::HashObj<DescType> const& key, castl::shared_ptr<ValType>& value)
				{
					value->Release();
				});
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