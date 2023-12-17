#include <header/ResourceImportingSystem.h>
#include <unordered_map>
#include <filesystem>
#include <SharedTools/header/LibraryExportCommon.h>

namespace resource_management
{

	using namespace std::filesystem;
	class ResourceImportingSystemImpl : public ResourceImportingSystem
	{
	public:
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
			for(auto& p : recursive_directory_iterator(rootPath))
			{
				if(p.is_regular_file())
				{
					auto postfix = p.path().extension().string();
					auto found = m_PostfixToImporterIndex.find(postfix);
					if(found != m_PostfixToImporterIndex.end())
					{
						auto importer = m_Importers[found->second];
						++m_ReservedSpace[found->second];
						m_ImportingResources[found->second].push_back(p.path().string());
					}
				}
			}
			m_ImportedResourcesMemoryList.resize(m_Importers.size());
			for (size_t i = 0; i < m_Importers.size(); ++i)
			{
				size_t resourceSize = m_Importers[i]->GetIResourceSizeInByte();
				m_ImportedResourcesMemoryList[i].resize(m_ReservedSpace[i] * resourceSize);
				for(uint32_t itrResource = 0; itrResource < m_ReservedSpace[i]; ++itrResource)
				{
					size_t offset = resourceSize * itrResource;
					m_Importers[i]->ImportResource(&m_ImportedResourcesMemoryList[i][offset]
						, m_ImportingResources[i][itrResource]);
				}
			}
		}
	private:
		std::unordered_map<std::string, uint32_t> m_PostfixToImporterIndex;
		std::vector<ResourceImporterBase*> m_Importers;
		std::vector<size_t> m_ReservedSpace;
		std::vector<std::vector<std::string>> m_ImportingResources;
		std::vector<std::vector<uint8_t>> m_ImportedResourcesMemoryList;
	};

	CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(ResourceImportingSystem, ResourceImportingSystemImpl)
}