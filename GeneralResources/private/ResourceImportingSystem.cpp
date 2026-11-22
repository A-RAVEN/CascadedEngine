#include <CAResource/ResourceImportingSystem.h>
#include <CAResource/ResourceManagingSystem.h>
#include <CAResource/ResourceSystemFactory.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CADeque.h>
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAMutex.h>
#include <filesystem>
//#include <LibraryExportCommon.h>
#define CA_IMPLEMENT_MODULE 1
#include <CACore/CAModuleImplementation.h>
#include <DebugUtils.h>
#include <FileLoader.h>

namespace resource_management
{
	using namespace castl;
	using namespace castl::filesystem;
	class ResourceImportingSystemImpl : public ResourceImportingSystem
	{
	public:
		virtual void SetResourceManager(ResourceManagingSystem* resourceManagingSystem) override
		{
			m_ResourceManagingSystem = resourceManagingSystem;
		}

		void Init(cacore::IModuleManager* pModuleManager)
		{
			m_ResourceManagingSystem = pModuleManager->GetInstance<ResourceManagingSystem>();
		}

		virtual void AddImporter(ResourceImporter* importer) override
		{
			if (importer == nullptr)
				return;
			m_ResourceImporters.push_back(importer);
		}

		virtual void ScanSourceDirectory(const castl::string& sourceDirectory) override
		{
			path callerSourceRoot(sourceDirectory, cafs::path::format::generic_format);
			path callerDestRoot(m_ResourceManagingSystem->GetResourceRootPath()
				, cafs::path::format::generic_format);

			if (!exists(callerSourceRoot))
			{
				// 源目录不存在也**不早退**：导入器可以有自己的根 / 回退，产出照样要注册、照样要落盘。
				CA_LOG_WARN("ScanSourceDirectory：源目录不存在 {}（导入器仍会尝试自己声明的根）"
					, callerSourceRoot.generic_string());
			}

			for (ResourceImporter* importer : m_ResourceImporters)
			{
				// 源 / 目标根：导入器声明了就用它，否则用这次扫描传入的（声明空 = 跟随）。
				castl::string const declaredSource = importer->GetSourceDirectory();
				path sourceRoot = declaredSource.empty()
					? callerSourceRoot
					: path(declaredSource, cafs::path::format::generic_format);
				castl::string const declaredDest = importer->GetDestDirectory();
				path destRoot = declaredDest.empty()
					? callerDestRoot
					: path(declaredDest, cafs::path::format::generic_format);
				// 原生导入器从这里读有效根（C# 反向实现读不到，必须自己声明目录）。
				importer->SetScanDirectories(sourceRoot.generic_string(), destRoot.generic_string());

				// 拉取式：先拿迭代器，再逐条 HasNext → (NextPath, NextResource)。
				// 迭代器归管线所有（C# 反向实现的代理在析构里放掉托管保活，与 CreateResource 同构）。
				castl::shared_ptr<ResourceImporterProductions> productions(
					importer->Produce(),
					[](ResourceImporterProductions* releasedProductions) { delete releasedProductions; });
				if (!productions)
				{
					CA_LOG_WARN("ScanSourceDirectory：导入器没有交出产出迭代器（标签 {}）", importer->GetTags());
					continue;
				}
				while (productions->HasNext())
				{
					castl::string const relativePath = productions->NextPath();
					IResource* produced = productions->NextResource();
					if (produced == nullptr)
					{
						throw resource_creation_error("导入器产出了空资源（路径 " + relativePath +
							"，标签 " + importer->GetTags() + "）");
					}
					// 单一强所有者：注册表里这一份 shared_ptr（与 CreateResource 逐字同构）。
					castl::shared_ptr<IResource> resource(produced,
						[](IResource* releasedObj) { delete releasedObj; });
					// 重导入替换：先放掉同路径的旧条目再注册。MUST NOT 依赖 RegisterResource 的
					// get_or_create —— 那会把旧对象静默留下来，重扫之后消费者仍拿到旧库。
					m_ResourceManagingSystem->ReleaseResource(relativePath);
					m_ResourceManagingSystem->RegisterResource(relativePath, resource);
				}
			}
			// 无论源目录在不在、导入器有没有产出：本轮的注册内容都要落盘（旧路径的早退曾跳过这里）。
			m_ResourceManagingSystem->SerializeAll();
		}
	private:
		castl::vector<ResourceImporter*> m_ResourceImporters;
		ResourceManagingSystem* m_ResourceManagingSystem = nullptr;
	};

	class ResourceManagingSystem_Impl : public ResourceManagingSystem
	{
	public:
		void Init(cacore::IModuleManager* pModuleManager)
		{
			pIOManager = pModuleManager->GetInstance<ca_io::IOManager>();
		}


		void Initialize(castl::shared_ptr<ca_io::IOManager> ioManager) override
		{
			pIOManager = ioManager.get();
		}
		void SerializeAll() override
		{
			m_PathToResource.for_each([&](cacore::PathHash const& key, castl::shared_ptr<IResource>& val)
			{
				castl::filesystem::path destPath = m_AssetRootPath / key.Get();
				castl::filesystem::create_directories(destPath.parent_path());
				auto batch = pIOManager->WriteBatch(destPath.generic_string());
				val->Serialize(batch.get());
				batch->SubmitAndWait();
			});
		}
		void SetResourceRootPath(castl::string const& path) override
		{
			m_AssetRootPath = path;
		}
		castl::string GetResourceRootPath() const override
		{
			return m_AssetRootPath.generic_string();
		}
		castl::string GetResourceFullPath(castl::string const& path) const override
		{
			return (m_AssetRootPath / path).generic_string();
		}
		castl::shared_ptr<IResource> GetOrLoadResource(cacore::PathHash const& path
			, castl::function<IResource* ()> newCallback, castl::function<void(IResource*)> deleteCallback) override
		{
			auto result = m_PathToResource.get_or_create(path, [&](auto& pathObj)
				{
					CA_LOG("Load new resource: {}", path);
					castl::shared_ptr<IResource> newRes = castl::shared_ptr<IResource>(newCallback(), deleteCallback);
					if (!newRes)
					{
						// design D4：Create/构造失败不许把空对象留在表里再解引用（旧路径这里直接 Deserialize 会崩）。
						throw resource_creation_error("加载资源失败：构造函数返回了空指针（路径 " +
							castl::string(path.Get()) + "）");
					}
					auto batch = pIOManager->Batch(GetResourceFullPath(path));
					newRes->Deserialize(batch.get());
					return newRes;
				});
			return result->second;
		}
		castl::shared_ptr<IResource> GetOrNewResource(cacore::PathHash const& path
			, castl::function<IResource* ()> newCallback, castl::function<void(IResource*)> deleteCallback) override
		{
			auto result = m_PathToResource.get_or_create(path, [&](auto& pathObj)
				{
					CA_LOG("Create new resource: {}", path);
					castl::shared_ptr<IResource> newRes = castl::shared_ptr<IResource>(newCallback(), deleteCallback);
					if (!newRes)
					{
						throw resource_creation_error("新建资源失败：构造函数返回了空指针（路径 " +
							castl::string(path.Get()) + "）");
					}
					return newRes;
				});
			return result->second;
		}

		// ---- 注册式工厂构造（csharp-reverse-interface-binding 4.3 / 6.1）----
		// 一条工厂登记 = 借用指针 + 可选的强所有物 + 反向工厂的（句柄, 释放回调）。
		// 强所有者只有一处：原生工厂是 `owned` 这份 shared_ptr；反向工厂是**创建模块的句柄表**那一条
		// （这里只持借用指针与释放能力）。句柄 MUST NOT 丢 —— 注销 / 替换时靠它 + release 放掉强引用。
		struct FactoryRecord
		{
			IResourceFactory* factory = nullptr;
			castl::shared_ptr<IResourceFactory> owned;
			uint64_t handle = 0;
			ResourceFactoryRelease release = nullptr;
		};

		void ReleaseFactoryRecord(FactoryRecord& record)
		{
			if (record.release != nullptr)
			{
				// 在创建模块里放强引用：代理析构 → releaseUser → 托管 GCHandle.Free。
				ResourceFactoryRelease release = record.release;
				record.release = nullptr;
				release(record.handle);
			}
			record.owned.reset();
			record.factory = nullptr;
			record.handle = 0;
		}

		void RegisterFactory(castl::string const& typeName, uint64_t handle,
		                     IResourceFactory* factory, ResourceFactoryRelease release) override
		{
			if (handle == 0 || factory == nullptr || release == nullptr)
			{
				throw resource_creation_error(
					"注册资源工厂失败：句柄入口必须给 (非 0 句柄, 借用工厂指针, 释放回调)（类型 " + typeName + "）");
			}
			FactoryRecord record;
			record.factory = factory;
			record.handle = handle;
			record.release = release;
			// 同类型名再注册 = 替换：先释放旧记录（反向工厂在这里析构）。
			auto previous = m_TypeToFactory.find(typeName);
			if (previous != m_TypeToFactory.end())
			{
				ReleaseFactoryRecord(previous->second);
			}
			m_TypeToFactory[typeName] = record;
		}
		void RegisterFactory(castl::string const& typeName, castl::shared_ptr<IResourceFactory> factory) override
		{
			if (!factory)
			{
				throw resource_creation_error("注册资源工厂失败：传入了空工厂（类型 " + typeName + "）");
			}
			FactoryRecord record;
			record.factory = factory.get();
			record.owned = factory;
			auto previous = m_TypeToFactory.find(typeName);
			if (previous != m_TypeToFactory.end())
			{
				ReleaseFactoryRecord(previous->second);
			}
			m_TypeToFactory[typeName] = record;
		}
		IResourceFactory* GetFactory(castl::string const& typeName) override
		{
			auto found = m_TypeToFactory.find(typeName);
			return found == m_TypeToFactory.end() ? nullptr : found->second.factory;
		}
		bool HasFactory(castl::string const& typeName) const override
		{
			return m_TypeToFactory.find(typeName) != m_TypeToFactory.end();
		}
		bool UnregisterFactory(castl::string const& typeName) override
		{
			auto found = m_TypeToFactory.find(typeName);
			if (found == m_TypeToFactory.end())
			{
				return false;
			}
			ReleaseFactoryRecord(found->second);
			m_TypeToFactory.erase(found);
			return true;
		}

		void RegisterResource(castl::string const& path, castl::shared_ptr<IResource> resource) override
		{
			if (!resource)
			{
				// 可观测契约：MUST NOT 注册空资源；调用方应先用 HasResource 读数确认没留下空对象。
				throw resource_creation_error("注册资源失败：传入了空资源（路径 " + path + "）");
			}
			cacore::PathHash key(path);
			m_PathToResource.get_or_create(key, [&](cacore::PathHash const&)
				{
					return resource;
				});
		}
		bool HasResource(castl::string const& path) const override
		{
			return m_PathToResource.try_get(cacore::PathHash(path)) != nullptr;
		}
		castl::shared_ptr<IResource> GetResource(castl::string const& path) const override
		{
			castl::shared_ptr<IResource> const* found =
				m_PathToResource.try_get(cacore::PathHash(path));
			return found == nullptr ? castl::shared_ptr<IResource>() : *found;
		}
		bool ReleaseResource(castl::string const& path) override
		{
			return m_PathToResource.try_erase(cacore::PathHash(path));
		}
		castl::shared_ptr<IResource> CreateResource(castl::string const& typeName,
		                                            castl::string const& path) override
		{
			IResourceFactory* factory = GetFactory(typeName);
			if (factory == nullptr)
			{
				throw resource_creation_error("未注册的资源工厂类型：\"" + typeName + "\"（路径 " + path + "）");
			}
			IResource* created = nullptr;
			try
			{
				created = factory->Create();
			}
			catch (resource_creation_error const&)
			{
				throw;
			}
			catch (std::exception const& error)
			{
				throw resource_creation_error("资源工厂 Create 失败（类型 \"" + typeName + "\"）：" +
					castl::string(error.what()));
			}
			catch (...)
			{
				throw resource_creation_error("资源工厂 Create 抛出未知异常（类型 \"" + typeName + "\"）");
			}
			if (created == nullptr)
			{
				// 明确的可观测失败：不包 shared_ptr、不注册空对象。
				throw resource_creation_error("资源工厂 Create 返回了空指针（类型 \"" + typeName + "\"）");
			}
			// 单一所有者：这里这份 shared_ptr 就是资源对象的唯一强引用；deleter 走虚析构（反向代理在
			// 析构里 releaseUser → 托管 GCHandle.Free）。新建路径 MUST NOT Deserialize。
			castl::shared_ptr<IResource> resource(created, [](IResource* releasedObj) { delete releasedObj; });
			RegisterResource(path, resource);
			return resource;
		}
	private:
		castl::filesystem::path m_AssetRootPath;
		ca_io::IOManager* pIOManager;
		castl::shared_dic<cacore::PathHash, castl::shared_ptr<IResource>> m_PathToResource;
		castl::unordered_map<castl::string, FactoryRecord> m_TypeToFactory;
	};

	class ResourceFactoryImpl : public ResourceFactory
	{
	public:
		virtual ResourceImportingSystem* NewImportingSystem() override
		{
			return new ResourceImportingSystemImpl();
		}
		virtual void DeleteImportingSystem(ResourceImportingSystem* releasingSystem) override
		{
			delete releasingSystem;
		}
		virtual ResourceManagingSystem* NewManagingSystem() override
		{
			return new ResourceManagingSystem_Impl();
		}
		virtual void DeleteManagingSystem(ResourceManagingSystem* releasingSystem) override
		{
			delete releasingSystem;
		}
	};

	//CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(ResourceFactory, ResourceFactoryImpl);
}

CA_MODULE_INSTANCE(resource_management::ResourceImportingSystem, resource_management::ResourceImportingSystemImpl, ResourceImportingSystem);
CA_MODULE_INSTANCE(resource_management::ResourceManagingSystem, resource_management::ResourceManagingSystem_Impl, ResourceManagingSystem);
