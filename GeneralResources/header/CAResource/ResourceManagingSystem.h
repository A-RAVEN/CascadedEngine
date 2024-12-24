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
		virtual castl::string GetResourceFullPath(castl::string const& path) const = 0;
	protected:
		virtual castl::shared_ptr<IResource> GetOrLoadResource(castl::string const& path
			, castl::function<IResource*()> newCallback, castl::function<void(IResource*)> deleteCallback) = 0;
		virtual castl::shared_ptr<IResource> GetOrNewResource(castl::string const& path
			, castl::function<IResource* ()> newCallback, castl::function<void(IResource*)> deleteCallback) = 0;
	public:
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
		}
	};
}