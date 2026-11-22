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

		virtual void AddImporter(ResourceImporterBase* importer) override
		{
			auto postfix = importer->GetSourceFilePostfix();
			if(m_PostfixToImporterIndex.find(postfix) == m_PostfixToImporterIndex.end())
			{
				m_PostfixToImporterIndex[postfix] = m_Importers.size();
				m_Importers.push_back(importer);
			}
		}

		virtual void AddImporter(ResourceImporterFree* importer) override
		{
			if (importer == nullptr)
				return;
			m_GeneralImporters.push_back(importer);
		}

		virtual void ScanSourceDirectory(const castl::string& sourceDirectory) override
		{
			path rootPath(sourceDirectory, cafs::path::format::generic_format);
			castl::filesystem::path targetRootPath = m_ResourceManagingSystem->GetResourceRootPath();

			for (ResourceImporterFree* importer : m_GeneralImporters)
			{
				importer->ImportResource(m_ResourceManagingSystem, rootPath, targetRootPath);
			}

			if(!exists(rootPath))
			{
				return;
			}
			m_ReservedSpace.resize(m_Importers.size());
			m_ImportingResources.resize(m_Importers.size());
			castl::fill(m_ReservedSpace.begin(), m_ReservedSpace.end(), 0);
			for (auto& strVec : m_ImportingResources)
			{
				strVec.clear();
			}
			for(auto& p : recursive_directory_iterator(rootPath))
			{
				if(p.is_regular_file())
				{
					auto postfix = p.path().extension().generic_string();
					auto found = m_PostfixToImporterIndex.find(postfix);
					if(found != m_PostfixToImporterIndex.end())
					{
						auto importer = m_Importers[found->second];

						auto relativePath = castl::filesystem::relative(p.path(), rootPath);
						relativePath.replace_extension(importer->GetDestFilePostfix());
						//relativePath.replace_extension("");
						auto destPath = targetRootPath / relativePath;

						bool needImport = true;
						if (castl::filesystem::exists(destPath))
						{
							bool hasFile = true;
							auto targetTime = castl::filesystem::last_write_time(destPath);
							if (!destPath.has_extension())
							{
								hasFile = false;
								using Entry = castl::filesystem::directory_entry;
								for (Entry const& entry : castl::filesystem::directory_iterator(destPath))
								{
									hasFile = true;
									targetTime = targetTime < entry.last_write_time() ? targetTime : entry.last_write_time();
								}
							}

							needImport = (!hasFile) || targetTime < p.last_write_time();
						}
						if (needImport)
						{
							++m_ReservedSpace[found->second];
							m_ImportingResources[found->second].push_back(castl::make_pair(p.path(), relativePath));
						}
					}
				}
			}
			for (size_t i = 0; i < m_Importers.size(); ++i)
			{
				for(uint32_t itrResource = 0; itrResource < m_ReservedSpace[i]; ++itrResource)
				{
					m_Importers[i]->ImportResource(m_ResourceManagingSystem
						, m_ImportingResources[i][itrResource].first.generic_string()
						, m_ImportingResources[i][itrResource].second.generic_string());
				}
			}
			m_ResourceManagingSystem->SerializeAll();
		}
	private:
		castl::unordered_map<castl::string, uint32_t> m_PostfixToImporterIndex;
		castl::vector<ResourceImporterFree*> m_GeneralImporters;
		castl::vector<ResourceImporterBase*> m_Importers;
		castl::vector<size_t> m_ReservedSpace;
		castl::vector<castl::vector<castl::pair<castl::filesystem::path, castl::filesystem::path>>> m_ImportingResources;
		ResourceManagingSystem* m_ResourceManagingSystem;
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
