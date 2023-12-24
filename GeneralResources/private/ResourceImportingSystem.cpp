#include <header/ResourceImportingSystem.h>
#include <header/ResourceManagingSystem.h>
#include <header/ResourceSystemFactory.h>
#include <unordered_map>
#include <filesystem>
#include <deque>
#include <SharedTools/header/LibraryExportCommon.h>
#include <SharedTools/header/DebugUtils.h>
#include <SharedTools/header/FileLoader.h>

namespace resource_management
{

	using namespace std::filesystem;
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

		virtual void ScanSourceDirectory(const std::string& sourceDirectory) override
		{
			path rootPath(sourceDirectory);
			if(!exists(rootPath))
			{
				return;
			}
			m_ReservedSpace.resize(m_Importers.size());
			m_ImportingResources.resize(m_Importers.size());
			std::fill(m_ReservedSpace.begin(), m_ReservedSpace.end(), 0);
			for (auto& strVec : m_ImportingResources)
			{
				strVec.clear();
			}
			std::filesystem::path targetRootPath = m_ResourceManagingSystem->SetResourceRootPath();
			for(auto& p : recursive_directory_iterator(rootPath))
			{
				if(p.is_regular_file())
				{
					auto postfix = p.path().extension().string();
					auto found = m_PostfixToImporterIndex.find(postfix);
					if(found != m_PostfixToImporterIndex.end())
					{
						auto importer = m_Importers[found->second];

						auto relativePath = std::filesystem::relative(p.path(), rootPath);
						relativePath.replace_extension(importer->GetDestFilePostfix());
						auto destPath = targetRootPath / relativePath;

						bool needImport = true;
						if (std::filesystem::exists(destPath))
						{
							auto targetTime = std::filesystem::last_write_time(destPath);
							needImport = targetTime < p.last_write_time();
						}
						if (needImport)
						{
							++m_ReservedSpace[found->second];
							m_ImportingResources[found->second].push_back(std::make_pair(p.path(), relativePath));
						}
					}
				}
			}
			for (size_t i = 0; i < m_Importers.size(); ++i)
			{
				for(uint32_t itrResource = 0; itrResource < m_ReservedSpace[i]; ++itrResource)
				{
					m_Importers[i]->ImportResource(m_ResourceManagingSystem
						, m_ImportingResources[i][itrResource].first.string()
						, m_ImportingResources[i][itrResource].second.string());
				}
			}
			m_ResourceManagingSystem->SerializeAllResources();
		}
	private:
		std::unordered_map<std::string, uint32_t> m_PostfixToImporterIndex;
		std::vector<ResourceImporterBase*> m_Importers;
		std::vector<size_t> m_ReservedSpace;
		std::vector<std::vector<std::pair<std::filesystem::path, std::filesystem::path>>> m_ImportingResources;
		ResourceManagingSystem* m_ResourceManagingSystem;
	};

	class ResourceManagingSystemImpl : public ResourceManagingSystem
	{
	public:
		struct ChunkedMemoryAllocator
		{
		public:
			ChunkedMemoryAllocator(size_t page_size, size_t chunk_size) : m_PageSize(page_size)
				, m_ChunkSizeByte(chunk_size), m_LastPageUsedChunkCount(m_PageSize)
			{

			}

			void* AllocChunk()
			{
				if (m_AvailableChunks.empty())
				{
					return AllocNew();
				}
				auto front = m_AvailableChunks.front();
				m_AvailableChunks.pop_front();
				return front;
			}

			void ReleaseChunk(void* releasedChunk)
			{
				m_AvailableChunks.push_back(releasedChunk);
			}
		private:
			void* AllocNew()
			{
				if (m_LastPageUsedChunkCount == m_PageSize)
				{
					m_MemoryPages.emplace_back();
					m_MemoryPages.back().resize(m_PageSize * m_ChunkSizeByte);
					m_LastPageUsedChunkCount = 0;
				}
				void* result = m_MemoryPages.back().data() + m_LastPageUsedChunkCount * m_ChunkSizeByte;
				++m_LastPageUsedChunkCount;
				return result;
			}
		private:
			size_t m_PageSize;
			size_t m_ChunkSizeByte;
			std::vector<std::vector<uint8_t>> m_MemoryPages;
			size_t m_LastPageUsedChunkCount;
			std::deque<void*> m_AvailableChunks;
		};

		virtual void SetResourceRootPath(std::string const& path) override
		{
			m_AssetRootPath = path;
		}

		virtual std::string SetResourceRootPath() const
		{
			return m_AssetRootPath.string();
		}

		virtual IResource* TryGetResource(std::string const& path) override
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			IResource* result = nullptr;
			auto find = m_AssetPathToResourceAddress.find(path);
			if (find != m_AssetPathToResourceAddress.end())
			{
				return static_cast<IResource*>(find->second);
			}
			return nullptr;
		}

		virtual std::vector<std::byte> LoadBinaryFile(std::string const& path) override
		{
			auto resourcePath = m_AssetRootPath / path;
			return fileloading_utils::LoadBinaryFile(resourcePath.string());
		}

		virtual void* AllocResourceMemory(
			std::string type_name
			, std::string const& resource_path
			, uint64_t size_in_bytes) override
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			auto found = m_TypeNameToMemoryAllocator.find(type_name);
			if (found == m_TypeNameToMemoryAllocator.end())
			{
				m_TypeNameToMemoryAllocator.insert(std::make_pair(type_name, ChunkedMemoryAllocator{ 16, size_in_bytes }));
				found = m_TypeNameToMemoryAllocator.find(type_name);
			}

			void* result = found->second.AllocChunk();
			if (resource_path != "")
			{
				CA_ASSERT(m_AssetPathToResourceAddress.find(resource_path) == m_AssetPathToResourceAddress.end(), "Asset Already Exist!");
				CA_ASSERT(m_ResourceAddressToAssetPath.find(result) == m_ResourceAddressToAssetPath.end(), "Asset Already Exist!");
				m_AssetPathToResourceAddress[resource_path] = result;
				m_ResourceAddressToAssetPath[result] = resource_path;
			}

			return result;
		}
		virtual void ReleaseResourceMemory(std::string type_name, void* pointer) override
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			auto found = m_TypeNameToMemoryAllocator.find(type_name);
			CA_ASSERT(found != m_TypeNameToMemoryAllocator.end(), "try release memory to void: " + type_name);
			found->second.ReleaseChunk(pointer);

			auto resourceToPath = m_ResourceAddressToAssetPath.find(pointer);
			if (resourceToPath != m_ResourceAddressToAssetPath.end())
			{
				m_AssetPathToResourceAddress.erase(resourceToPath->second);
				m_ResourceAddressToAssetPath.erase(pointer);
			}
		}

		virtual void SerializeAllResources() override
		{
			for (auto& pair : m_ResourceAddressToAssetPath)
			{
				std::filesystem::path destPath = m_AssetRootPath / pair.second;
				std::filesystem::create_directories(destPath.parent_path());
				IResource* resource = static_cast<IResource*>(pair.first);
				std::vector<std::byte> serializedData;
				resource->Serialzie(serializedData);
				fileloading_utils::WriteBinaryFile(destPath.string(), serializedData.data(), serializedData.size());
			}
		}
	private:
		std::mutex m_Mutex;
		std::unordered_map<std::string, ChunkedMemoryAllocator> m_TypeNameToMemoryAllocator;
		std::unordered_map<std::string, void*> m_AssetPathToResourceAddress;
		std::unordered_map<void*, std::string> m_ResourceAddressToAssetPath;

		std::filesystem::path m_AssetRootPath;
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
			return new ResourceManagingSystemImpl();
		}
		virtual void DeleteManagingSystem(ResourceManagingSystem* releasingSystem) override
		{
			delete releasingSystem;
		}
	};

	CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(ResourceFactory, ResourceFactoryImpl);
}