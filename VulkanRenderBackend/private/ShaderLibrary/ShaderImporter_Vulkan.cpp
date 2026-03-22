#include "ShaderImporter_Vulkan.h"
#include <CAResource/ResourceManagingSystem.h>
#include <CASTL/CAUnorderedSet.h>
#include <CAResource/IResource.h>
#include "ShaderLibrary.h"

namespace graphics_backend
{
	void VKShaderResourceImporter::ImportResource(ResourceManagingSystem* resourceManager
		, cafs::path const& sourcePath
		, cafs::path const& destPath)
	{
		if (!cafs::exists(sourcePath))
		{
			return;
		}
		castl::cout << CATypeDescriptor<ShaderLibrary>::member_count << castl::endl;
		castl::unordered_map<cahash::sha256_hash::result_type, ShaderCode> shaderPrograms;

		cafs::path shaderLibraryPath = "VKShaderLibrary.shLib";

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
					auto shaderpath = relative_path;
					shaderpath.replace_extension("");

					auto pCompiler = m_ShaderCompilerManager->AquireShaderCompilerShared();
					pCompiler->BeginCompileTask();
					pCompiler->AddInlcudePath(sourcePath.generic_string().c_str());
					pCompiler->AddSourceFile(p.path().generic_string().c_str());
					pCompiler->EnableDebugInfo();
					pCompiler->SetTarget(ShaderCompilerSlang::EShaderTargetType::eSpirV);
					pCompiler->Compile();
					if (pCompiler->HasError())
					{
						CA_LOG_ERR("Shader compile failed");
					}
					else
					{
						cacore::PathHash shaderPathHash = shaderpath;
						auto compileResults = pCompiler->GetResults();
						for (auto& result : compileResults)
						{
							if (result.targetType == ShaderCompilerSlang::EShaderTargetType::eSpirV)
							{
								auto& shaderInfo = shaderLibrary->m_ShaderFiles[shaderPathHash];
								shaderInfo.entryPointToShaderProgram.clear();
								shaderInfo.reflectionData = result.m_ReflectionData;
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
									cacore::NameHash entryPointName = program.entryPointName;
									shaderInfo.entryPointToShaderProgram.push_back(castl::make_pair(entryPointName, shaHash));
									found->second.sourceKeys.insert(ShaderSourceKey{ shaderPathHash, entryPointName });
								}


								///RegisterStructsInfo
								{
									for (auto& pairs : result.m_ReflectionData.m_ShaderStructs)
									{
										auto& name = pairs.first;
										auto& shaderStruct = pairs.second;
										if (name == CANAME("__Root"))
										{
			
											shaderLibrary->m_ShaderRootStructs.insert(castl::make_pair(shaderpath, shaderStruct));
											castl::cout << "Root Struct For " << shaderpath.generic_string() << castl::endl;
										}
										else if (shaderLibrary->m_ShaderStructs.find(name) == shaderLibrary->m_ShaderStructs.end())
										{
											castl::cout << "Add Shader Struct: " << name.Get() << castl::endl;
											shaderLibrary->m_ShaderStructs.insert(castl::make_pair(name, shaderStruct));
										}
										else
										{
										}
									}
								}
							}
						}
					}
					pCompiler->EndCompileTask();
				}
			}
		}
	}
}