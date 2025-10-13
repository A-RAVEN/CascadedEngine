#include <CAResource/ResourceImportingSystem.h>
#include <CAResource/ResourceManagingSystem.h>
#include <CAResource/ResourceSystemFactory.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CADeque.h>
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAMutex.h>
#include <filesystem>
#include <LibraryExportCommon.h>
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
					auto postfix = castl::to_ca(p.path().extension().generic_string());
					auto found = m_PostfixToImporterIndex.find(postfix);
					if(found != m_PostfixToImporterIndex.end())
					{
						auto importer = m_Importers[found->second];

						auto relativePath = castl::filesystem::relative(p.path(), rootPath);
						relativePath.replace_extension(castl::to_std(importer->GetDestFilePostfix()));
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
						, castl::to_ca(m_ImportingResources[i][itrResource].first.generic_string())
						, castl::to_ca(m_ImportingResources[i][itrResource].second.generic_string()));
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
		void Initialize(castl::shared_ptr<ca_io::IOManager> ioManager) override
		{
			pIOManager = ioManager;
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
					return newRes;
				});
			return result->second;
		}
	private:
		castl::filesystem::path m_AssetRootPath;
		castl::shared_ptr<ca_io::IOManager> pIOManager;
		castl::shared_dic<cacore::PathHash, castl::shared_ptr<IResource>> m_PathToResource;
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

	CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(ResourceFactory, ResourceFactoryImpl);
}