#pragma once
#include "ResourceImporter.h"
#include "IResource.h"
#include "IResourceFactory.h"
#include <ThreadManager.h>
#include <functional>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAString.h>
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

		// ---- 注册式工厂构造（csharp-reverse-interface-binding 6.1/4.3）----
		// 工厂注册表：按类型名登记工厂，运行时多态构造资源（取代编译期模板做"新建"）。
		// **句柄入口**（反向工厂用）：注册表**保留句柄**（MUST NOT 丢句柄），`factory` 是从创建模块
		// 的 `ResolveImpl` 拿到的**借用指针**，`release` 是同一个模块的 `ReleaseImpl` ——
		// 注销 / 替换时调 `release(handle)` 放掉强引用（代理析构 → releaseUser → GCHandle.Free）。
		// 强所有者是创建模块的句柄表那一条，注册表只持借用指针 + 释放能力（单一所有权不破）。
		// 原生 C++ 工厂用另一个重载（收 shared_ptr，同样不接受裸指针）。同类型名再注册 = 替换。
		virtual void RegisterFactory(castl::string const& typeName, uint64_t handle,
		                             IResourceFactory* factory, ResourceFactoryRelease release) = 0;
		virtual void RegisterFactory(castl::string const& typeName,
		                             castl::shared_ptr<IResourceFactory> factory) = 0;
		// 借用的工厂指针：生命周期随注册表（注销 / 替换后失效）。没有登记时返回 nullptr。
		virtual IResourceFactory* GetFactory(castl::string const& typeName) = 0;
		virtual bool HasFactory(castl::string const& typeName) const = 0;
		// 注销工厂：反向工厂在这里 `release(handle)`（代理析构 → releaseUser → GCHandle.Free）。
		virtual bool UnregisterFactory(castl::string const& typeName) = 0;

		// 注册工厂造出来的资源（**不收裸指针**）。资源注册表是资源对象的单一强所有者。
		virtual void RegisterResource(castl::string const& path, castl::shared_ptr<IResource> resource) = 0;
		virtual bool HasResource(castl::string const& path) const = 0;
		virtual castl::shared_ptr<IResource> GetResource(castl::string const& path) const = 0;
		// 从注册表释放一个资源（放掉唯一那份强引用；反向代理在这里析构）。
		virtual bool ReleaseResource(castl::string const& path) = 0;

		// 构造路径（新建，**不是加载**）：查工厂 → Create() → shared_ptr（虚析构 deleter，单一所有者）
		// → RegisterResource。**MUST NOT 调 Deserialize**（新建对象没有盘上数据）。
		// Create 抛异常 / 返回空 → 抛 resource_creation_error，且 MUST NOT 注册空资源。
		virtual castl::shared_ptr<IResource> CreateResource(castl::string const& typeName,
		                                                    castl::string const& path) = 0;

	protected:
		virtual castl::shared_ptr<IResource> GetOrLoadResource(cacore::PathHash const& path
			, castl::function<IResource*()> newCallback, castl::function<void(IResource*)> deleteCallback) = 0;
		virtual castl::shared_ptr<IResource> GetOrNewResource(cacore::PathHash const& path
			, castl::function<IResource*()> newCallback, castl::function<void(IResource*)> deleteCallback) = 0;
	public:
		template<typename TRes>
		castl::shared_ptr<TRes> GetOrLoadResource(cacore::PathHash const& path) requires std::is_base_of_v<IResource, TRes>
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

		template<typename TRes>
		castl::shared_ptr<TRes> GetOrLoadResource(cacore::PathHash const& path
			, castl::function<TRes* ()> newCallback, castl::function<void(IResource*)> deleteCallback) requires std::is_base_of_v<IResource, TRes>
		{
			castl::shared_ptr<IResource> loaded = GetOrLoadResource(path, newCallback, deleteCallback);
			return castl::static_pointer_cast<TRes>(loaded);
		}

		template<typename TRes, typename...TArgs>
		castl::shared_ptr<TRes> GetOrNewResource(cacore::PathHash const& path, TArgs&&...Args) requires std::is_base_of_v<IResource, TRes>
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
		castl::shared_ptr<TRes> GetOrNewSubResource(cafs::path const& path, castl::string const& postfix, TArgs&&...Args) requires std::is_base_of_v<IResource, TRes>
		{
			return GetOrNewResource<TRes, TArgs...>((path / postfix), castl::forward<TArgs>(Args)...);
		}
	};
}