#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <Utils/TypeTraits.h>
#include <Hasher.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAFunctional.h>
#include <CACore/CASharedDic.h>

namespace graphics_backend
{
	template<typename DescType, typename ValType>
		requires CanInitWithDesc<ValType, DescType>
	class HashDictionary : public D3D12SubobjectBase
	{
	public:
		HashDictionary(RenderBackend_D3D12& app) : D3D12SubobjectBase(app){}
		HashDictionary() = delete;
		HashDictionary(HashDictionary const& other) = delete;
		HashDictionary& operator=(HashDictionary const&) = delete;
		HashDictionary(HashDictionary&& other) noexcept = default;
		HashDictionary& operator=(HashDictionary&&) = default;

		castl::shared_ptr<ValType> GetOrCreate(cacore::HashObj<DescType> const& desc, castl::string const& name = "")
		{
			auto resultPair = m_InternalDic.get_or_create(desc, [&](auto inKey)
				{
					castl::shared_ptr<ValType> result = GetApp()->template NewSubObject_Shared<ValType>();
					return result;
				},
				[&](auto& kv_pair)
				{
					RenderBackend_D3D12::InitObj(kv_pair->second.get(), kv_pair->first.Get());
				});
			return resultPair->second;
		}

		void Foreach(castl::function<void(DescType const&, ValType*)> callbackFunc)
		{
			m_InternalDic.for_each([&](cacore::HashObj<DescType>const& key, castl::shared_ptr<ValType>& value)
				{
					callbackFunc(key.Get(), value.get());
				});
		}

		void ReleaseAll() requires CanRelease<ValType>
		{
			m_InternalDic.clear();
		}

		void Clear()
		{
			m_InternalDic.clear();
		}
	private:
		castl::shared_dic<cacore::HashObj<DescType>, castl::shared_ptr<ValType>> m_InternalDic;
	};
}