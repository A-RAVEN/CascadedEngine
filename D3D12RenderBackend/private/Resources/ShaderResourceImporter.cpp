#include "ShaderResourceImporter.h"
#include <CAResource/ResourceManagingSystem.h>
#include <CACore/CAHash.h>
#include <CASTL/CAUnorderedSet.h>

namespace graphics_backend
{

	struct ShaderCode
	{
		ECompileShaderType shaderType;
		castl::vector<uint8_t> data;
		castl::unordered_set<ShaderSourceKey> sourceKeys;
	};

	void ShaderResourceImporter::ImportResource(ResourceManagingSystem* resourceManager
		, cafs::path const& sourcePath
		, cafs::path const& destPath)
	{
		if (!cafs::exists(sourcePath))
		{
			return;
		}

		castl::unordered_map<cahash::sha256_hash::result_type, ShaderCode> shaderPrograms;

		cafs::path shaderLibraryPath = destPath / "DXShaderLibrary.shLib";

		auto shaderLibrary = resourceManager->GetOrNewResource<ShaderLibrary>(shaderLibraryPath.string());

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
					pCompiler->AddInlcudePath(sourcePath.string().c_str());
					pCompiler->AddSourceFile(p.path().string().c_str());
					pCompiler->EnableDebugInfo();
					pCompiler->SetTarget(ShaderCompilerSlang::EShaderTargetType::eDXIL);
					pCompiler->Compile();
					if (pCompiler->HasError())
					{
						CA_LOG_ERR("Shader compile failed");
					}
					else
					{
						//auto resource = resourceManager->GetOrNewResource<ShaderRes>(castl::to_ca(outPathWithExt.string()));
						auto compileResults = pCompiler->GetResults();
						for (auto& result : compileResults)
						{
							if (result.targetType == ShaderCompilerSlang::EShaderTargetType::eDXIL)
							{
								for (auto& program : result.programs)
								{
									auto shaHash = cahash::getHash<cahash::sha256_hash>(program.data.data(), program.data.size());
									auto found = shaderPrograms.find(shaHash);
									if (found == shaderPrograms.end())
									{
										ShaderCode shaderCode;
										shaderCode.data = program.data;
										shaderCode.shaderType = program.shaderType;
										found = shaderPrograms.insert(castl::make_pair(shaHash, shaderCode)).first;
									}
									found->second.sourceKeys.insert(ShaderSourceKey{ relative_path.string(), program.entryPointName });
								}

								//Add Vertex Attributes
								{
									cacore::defaultHasher<cahash::sha256_hash> hasher;
									for (auto& vertexAttributes : result.m_ReflectionData.m_VertexAttributes)
									{
										hasher.hash(vertexAttributes);
									}
									auto vertexAttributesHash = hasher.getHash();
									castl::unordered_map<cahash::sha256_hash::result_type, castl::vector<ShaderCompilerSlang::ShaderVertexAttributeData>> vertexAttributesMap;
								}

								//Add Binding Data
								{
									cacore::defaultHasher<cahash::sha256_hash> hasher;
									for (auto& bindingData : result.m_ReflectionData.m_BindingData)
									{
										hasher.hash(bindingData);
									}
									auto bindingDataHash = hasher.getHash();
								}

								result.m_ReflectionData.m_BindingData;
							}
						}
						//resource->m_UniqueName = outPath;
					}
					pCompiler->EndCompileTask();
				}
			}
		}
	}
}