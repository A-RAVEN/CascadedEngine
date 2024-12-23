#pragma once
#include "ResourceImporter.h"
#include "IResource.h"
#include <ThreadManager.h>
#include <functional>
#include <CASTL/CASharedPtr.h>
#include <IOManager/IOManager.h>
#include <CACore/CASharedDic.h>

namespace resource_management
{

	class ResourceManagingSystem
	{
	public:
		virtual void Initialize(castl::shared_ptr<ca_io::IOManager> ioManager) = 0;
		virtual void SerializeAll() = 0;
		virtual void SetResourceRootPath(castl::string const& path) = 0;
		virtual castl::string GetResourceRootPath() const = 0;
		virtual castl::string GetResourceFullPath(castl::string_view path) const = 0;
		virtual castl::shared_ptr<IResource> GetOrLoadResource(castl::string const& path
			, castl::function<IResource*()> newCallback, castl::function<void(IResource*)> deleteCallback) = 0;
		virtual castl::shared_ptr<IResource> GetOrNewResource(castl::string const& path
			, castl::function<IResource* ()> newCallback, castl::function<void(IResource*)> deleteCallback) = 0;

		template<typename TRes>
		castl::shared_ptr<TRes> GetOrLoadResource(castl::string const& path) requires std::is_base_of_v<IResource, TRes>
		{
			castl::shared_ptr<IResource> loaded = GetOrLoadResource(path, [&]()
				{
					return new TRes();
				},
				[&](IResource* releasedObj)
				{
					delete releasedObj;
				});
			return castl::static_pointer_cast<TRes>(loaded);
		}

		template<typename TRes, typename...TArgs>
		castl::shared_ptr<TRes> GetOrNewResource(castl::string const& path, TArgs&&...Args) requires std::is_base_of_v<IResource, TRes>
		{
			castl::shared_ptr<IResource> newRes = GetOrNewResource(path, [&]()
				{
					return new TRes(castl::forward<TArgs>(Args)...);
				},
				[&](IResource* releasedObj)
				{
					delete releasedObj;
				});
			return castl::static_pointer_cast<TRes>(newRes);
		}

		template<typename TRes, typename...TArgs>
		castl::shared_ptr<TRes> GetOrNewSubResource(castl::string const& path, castl::string const& postfix, TArgs&&...Args) requires std::is_base_of_v<IResource, TRes>
		{
			return GetOrNewResource<TRes, TArgs...>((path + "/" + postfix), castl::forward<TArgs>(Args)...);
	/*		castl::shared_ptr<IResource> newRes = GetOrNewResource((path + "/" + postfix), [&]()
				{
					return new TRes(castl::forward<TArgs>(Args)...);
				},
				[&](TRes* releasedObj)
				{
					delete releasedObj;
				});
			return castl::const_pointer_cast<TRes>(newRes);*/
		}
	};

	//class ResourceManagingSystem1
	//{
	//public:
	//	void Initialize(castl::shared_ptr<ca_io::IOManager> ioManager)
	//	{
	//		pIOManager = ioManager;
	//	}

	//	virtual void SerializeAllResources() = 0;

	//	virtual void SetResourceRootPath(castl::string const& path) = 0;

	//	virtual castl::string GetResourceRootPath() const = 0;

	//	virtual castl::string ResourceFullPath(castl::string_view path) = 0;

	//	template<typename TRes>
	//	castl::shared_ptr<TRes> GetResource(castl::string_view const& path) requires std::is_base_of_v<IResource, TRes>
	//	{
	//		auto result = m_PathToResource.get_or_create(path, [&](auto& pathObj)
	//		{
	//			castl::shared_ptr<TRes> newRes =  castl::make_shared<TRes>(pathObj.Get());
	//			auto batch = pIOManager->Batch(ResourceFullPath(path));
	//			newRes->Deserialzie(batch.get());
	//		});
	//		return result->second;
	//	}

	//	template<typename TRes>
	//	void LoadResource(castl::string const& path, std::function<void(TRes*)> callback)
	//	{
	//		static_assert(std::is_base_of<IResource, TRes>::value, "Type T not derived from IResource");
	//		IResource* result = TryGetResource(path);
	//		if (result != nullptr)
	//		{
	//			callback(static_cast<TRes*>(result));
	//		}
	//		else
	//		{
	//			auto batch = pIOManager->Batch(ResourceFullPath(path));
	//			TRes* newResult = AllocResource<TRes>(path);
	//			newResult->Deserialzie(batch.get());
	//			callback(newResult);
	//		}
	//	}

	//	template<typename TRes, typename...TArgs>
	//	TRes* AllocResource(castl::string const& outPath, TArgs&&...Args) {
	//		static_assert(std::is_base_of<IResource, TRes>::value, "Type T not derived from IResource");
	//		uint64_t allocSize = sizeof(TRes);
	//		auto address = AllocResourceMemory(typeid(TRes).name(), outPath, allocSize);
	//		TRes* result = new (address) TRes(std::forward<TArgs>(Args)...);
	//		return result;
	//	}

	//	template<typename TRes, typename...TArgs>
	//	TRes* AllocSubResource(castl::string const& outPath, castl::string const& postfix, TArgs&&...Args) {
	//		static_assert(std::is_base_of<IResource, TRes>::value, "Type T not derived from IResource");
	//		uint64_t allocSize = sizeof(TRes);
	//		auto address = AllocResourceMemory(typeid(TRes).name(), (outPath + "/" + postfix), allocSize);
	//		TRes* result =  new (address) TRes(std::forward<TArgs>(Args)...);
	//		return result;
	//	}

	//	//template<typename TRes>
	//	//void ReleaseResource(TRes* releasingRes)
	//	//{
	//	//	static_assert(std::is_base_of<IResource, TRes>::value, "Type T not derived from IResource");
	//	//	ReleaseResourceMemory(typeid(TRes).name(), releasingRes);
	//	//}

	//	castl::shared_ptr<ca_io::IOManager> pIOManager;
	//	castl::shared_dic<castl::string, castl::shared_ptr<IResource>> m_PathToResource;
	//};

}