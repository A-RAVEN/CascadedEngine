#include "ShaderImporter_Vulkan.h"
#include <CAResource/ResourceManagingSystem.h>
#include <CASTL/CAUnorderedSet.h>
#include <CAResource/IResource.h>

namespace graphics_backend
{

	struct ShaderSourceKey
	{
		castl::string pathToFile;
		castl::string entryPoint;
		auto operator<=>(const ShaderSourceKey&) const = default;
	};

	struct ShaderCode
	{
		ECompileShaderType shaderType;
		castl::vector<uint8_t> data;
		castl::unordered_set<ShaderSourceKey> sourceKeys;
	};

	class ShaderLibrary : public resource_management::IResource
	{
	public:
		//castl::unordered_map<ShaderSourceKey, castl::shared_ptr<ShaderResource>> m_ShaderResources;
		castl::unordered_map<cahash::sha256_hash::result_type, ShaderCode> m_ShaderPrograms;
		castl::unordered_map<cacore::NameHash, ShaderCompilerSlang::ShaderStructData> m_ShaderStructs;
		virtual void Serialize(ca_io::WBatch* inWriter) override {}
		virtual void Deserialize(ca_io::IOBatch* inReader) override {}
	};

	void VKShaderResourceImporter::ImportResource(ResourceManagingSystem* resourceManager
		, cafs::path const& sourcePath
		, cafs::path const& destPath)
	{
		if (!cafs::exists(sourcePath))
		{
			return;
		}

		castl::unordered_map<cahash::sha256_hash::result_type, ShaderCode> shaderPrograms;

		cafs::path shaderLibraryPath = destPath / "VKShaderLibrary.shLib";

		auto shaderLibrary = resourceManager->GetOrNewResource<ShaderLibrary>(shaderLibraryPath.generic_string());
		shaderLibrary->m_ShaderPrograms.clear();

		for (auto& p : cafs::recursive_directory_iterator(sourcePath))
		{
			if (p.is_regular_file())
			{
				auto postfix = p.path().extension();
				if (postfix == ".slang")
				{
					auto relative_path = cafs::relative(p.path(), sourcePath);

					auto pCompiler = m_ShaderCompilerManager->AquireShaderCompilerShared();
					pCompiler->BeginCompileTask();
					pCompiler->AddInlcudePath(sourcePath.generic_string().c_str());
					pCompiler->AddSourceFile(p.path().generic_string().c_str());
					pCompiler->EnableDebugInfo();
					pCompiler->SetTarget(ShaderCompilerSlang::EShaderTargetType::eDXIL);
					pCompiler->Compile();
					if (pCompiler->HasError())
					{
						CA_LOG_ERR("Shader compile failed");
					}
					else
					{
						auto compileResults = pCompiler->GetResults();
						for (auto& result : compileResults)
						{
							if (result.targetType == ShaderCompilerSlang::EShaderTargetType::eDXIL)
							{
								for (auto& program : result.programs)
								{
									auto shaHash = cahash::getHash<cahash::sha256_hash>(program.data.data(), program.data.size());
									auto found = shaderLibrary->m_ShaderPrograms.find(shaHash);
									if (found == shaderLibrary->m_ShaderPrograms.end())
									{
										ShaderCode shaderCode;
										shaderCode.data = program.data;
										shaderCode.shaderType = program.shaderType;
										found = shaderLibrary->m_ShaderPrograms.insert(castl::make_pair(shaHash, shaderCode)).first;
									}


									std::cout << relative_path.generic_string() << ":" << shaHash.toString() << std::endl;
									found->second.sourceKeys.insert(ShaderSourceKey{ relative_path.generic_string(), program.entryPointName });
								}


								///RegisterStructsInfo
								{
									for (auto& pairs : result.m_ReflectionData.m_ShaderStructs)
									{
										auto& name = pairs.first;
										auto& shaderStruct = pairs.second;
										if (shaderLibrary->m_ShaderStructs.find(name) == shaderLibrary->m_ShaderStructs.end())
										{
											shaderLibrary->m_ShaderStructs.insert(castl::make_pair(name, shaderStruct));
										}
										else
										{
											CA_LOG_ERR("Struct already exists");
										}
									}
								}

								////Add Vertex Attributes
								//{
								//	cacore::aggregateHasher<cahash::sha256_hash> hasher;
								//	for (auto& vertexAttributes : result.m_ReflectionData.m_VertexAttributes)
								//	{
								//		hasher.hash(vertexAttributes);
								//	}
								//	auto vertexAttributesHash = hasher.getHash();
								//	castl::unordered_map<cahash::sha256_hash::result_type, castl::vector<ShaderCompilerSlang::ShaderVertexAttributeData>> vertexAttributesMap;
								//}

								//Add Binding Data
								//Prepare Constant Buffer Data
								{
									cacore::aggregateHasher<cahash::sha256_hash> hasher;
									for (auto& bindingData : result.m_ReflectionData.m_BindingData)
									{

										hasher.hash(bindingData);
									}
									auto bindingDataHash = hasher.getHash();
								}

								result.m_ReflectionData.m_BindingData;
							}
						}
					}
					pCompiler->EndCompileTask();
				}
			}
		}
	}
}