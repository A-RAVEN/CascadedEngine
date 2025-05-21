#include "ShaderImporter_D12.h"
#include <CAResource/ResourceManagingSystem.h>
#include <CASTL/CAUnorderedSet.h>
#include <CAResource/IResource.h>
#include "ShaderLibrary.h"
#include <CASTL/CADeque.h>

namespace graphics_backend
{
	struct CBufferBindingInfo
	{
		uint32_t bindingID;
		cacore::NameHash cbufferStructName;
	};

	struct ImageBindingInfo
	{
		ShaderCompilerSlang::EShaderResourceType resourceType;
		ShaderCompilerSlang::EShaderResourceAccess accessType;
		bool isUAV() const
		{
			return (resourceType == ShaderCompilerSlang::EShaderResourceType::eRWTexture) ||
				(resourceType == ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer);
		}
		uint32_t bindingID;
		cacore::NameHash imageBindingName;
		int imageCount;
	};

	struct BufferBindingInfo
	{
		ShaderCompilerSlang::EShaderResourceType resourceType;
		ShaderCompilerSlang::EShaderResourceAccess accessType;
		bool isUAV() const
		{
			return (resourceType == ShaderCompilerSlang::EShaderResourceType::eRWTexture) ||
				(resourceType == ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer);
		}
		uint32_t bindingID;
		cacore::NameHash bufferBindingName;
		int bufferCount;
	};

	struct SamplerBindingInfo
	{
		uint32_t bindingID;
		cacore::NameHash samplerBindingName;
		int samplerCount;
	};

	struct StructBindingInfos
	{
		cacore::NameHash structBindingName;
		castl::vector<uint32_t> cbufferRefs;
		castl::vector<uint32_t> imageRefs;
		castl::vector<uint32_t> bufferRefs;
		castl::vector<uint32_t> samplerRefs;
		castl::vector<uint32_t> subStructs;
	};

	struct ShaderResourceBindingInfo
	{
		uint32_t spaceID;
		castl::vector<StructBindingInfos> structBindingInfos;
		castl::vector<CBufferBindingInfo> cbufferInfos;
		castl::vector<ImageBindingInfo> imageInfo;
		castl::vector<BufferBindingInfo> bufferInfos;
		castl::vector<SamplerBindingInfo> samplerInfos;

		StructBindingInfos& GetStruct(cacore::NameHash const& name)
		{
			for (StructBindingInfos& info : structBindingInfos)
			{
				if (info.structBindingName == name)
				{
					return info;
				}
			}
			structBindingInfos.push_back(StructBindingInfos{});
			structBindingInfos.back().structBindingName = name;
			return structBindingInfos.back();
		}
	};



	void ConstructShaderDescriptorInfo(ShaderCompilerSlang::ShaderReflectionData const& shaderReflectionData)
	{
		ShaderCompilerSlang::ShaderReflectionData const* p_ReflectionData = &shaderReflectionData;

		auto& spaceInfos = p_ReflectionData->m_BindingInfo.m_SpaceInfos;
		castl::vector<ShaderResourceBindingInfo> resourceBindingInfo;
		resourceBindingInfo.resize(spaceInfos.size());
		for (int spaceID = 0; spaceID < spaceInfos.size(); ++spaceID)
		{
			resourceBindingInfo[spaceID].spaceID = spaceID;
			auto& spaceInfo = spaceInfos[spaceID];
			auto& spaceStats = spaceInfo.m_ResourceStats;
			resourceBindingInfo[spaceID].cbufferInfos.resize(spaceStats.m_CBufferBindings.size());
		}

		auto& bindingInfo = p_ReflectionData->m_BindingInfo;

		auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];

		castl::deque<int> hierarchyIDs;
		hierarchyIDs.insert(hierarchyIDs.end(), rootHierarchy.m_SubBindingHierarchies.begin(), rootHierarchy.m_SubBindingHierarchies.end());
		while (!hierarchyIDs.empty())
		{
			uint32_t hierarchyID = hierarchyIDs.front();
			hierarchyIDs.pop_front();
			auto& hierarchy = bindingInfo.m_BindingDataHierarchies[hierarchyID];
			ShaderCompilerSlang::ShaderBindingHierarchy const* parentHierarchy = hierarchy.m_ParentID == -1 ? nullptr : &bindingInfo.m_BindingDataHierarchies[hierarchy.m_ParentID];
			hierarchyIDs.insert(hierarchyIDs.end(), hierarchy.m_SubBindingHierarchies.begin(), hierarchy.m_SubBindingHierarchies.end());
			cacore::NameHash structName = hierarchy.m_Name;

			//Collect Uniform Buffer
			if (hierarchy.m_SelfUniformBufferID != -1)
			{

				CA_ASSERT_BREAK(hierarchy.m_SelfUniformSpaceID != -1, "invalid uniform space id: {}", hierarchy.m_SelfUniformSpaceID);
				auto spaceID = hierarchy.m_SelfUniformSpaceID;
				auto uniformBufferID = hierarchy.m_SelfUniformSpaceID;
				auto& spaceResourceInfo = resourceBindingInfo[spaceID];
				auto& cbufferInfo = spaceResourceInfo.cbufferInfos[uniformBufferID];
				cbufferInfo.bindingID = hierarchy.m_SelfUniformBufferID;
				cbufferInfo.cbufferStructName = structName;
			}

			//Collect Resources
			if (!hierarchy.m_Bindings.empty())
			{
				//auto& bufferHandles = sourceStruct->GetBufferHandles();
				//auto& imageHandles = sourceStruct->GetImageHandles();
				//auto& samplerDescs = sourceStruct->GetSamplerDescriptors();

				for (auto& binding : hierarchy.m_Bindings)
				{
					auto spaceID = binding.m_BindingSpace;
					auto bindingID = binding.m_BindingID;
					auto& spaceResourceInfo = resourceBindingInfo[spaceID];
					switch (binding.m_ResourceType)
					{
					case ShaderCompilerSlang::EShaderResourceType::eTexture:
					case ShaderCompilerSlang::EShaderResourceType::eRWTexture:
					{
						ImageBindingInfo imageInfo{};
						imageInfo.accessType = binding.m_Access;
						imageInfo.resourceType = binding.m_ResourceType;
						imageInfo.bindingID = bindingID;
						imageInfo.imageBindingName = binding.m_Name;
						imageInfo.imageCount = binding.m_ElementCount;
						spaceResourceInfo.imageInfo.push_back(imageInfo);
						spaceResourceInfo.GetStruct(structName).imageRefs.push_back(spaceResourceInfo.imageInfo.size() - 1);
						break;
					}
					case ShaderCompilerSlang::EShaderResourceType::eStructuredBuffer:
					case ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer:
					{
						BufferBindingInfo bufferInfo{};
						bufferInfo.accessType = binding.m_Access;
						bufferInfo.resourceType = binding.m_ResourceType;
						bufferInfo.bindingID = bindingID;
						bufferInfo.bufferBindingName = binding.m_Name;
						bufferInfo.bufferCount = binding.m_ElementCount;
						spaceResourceInfo.bufferInfos.push_back(bufferInfo);
						spaceResourceInfo.GetStruct(structName).bufferRefs.push_back(spaceResourceInfo.bufferInfos.size() - 1);
						break;
					}
					case ShaderCompilerSlang::EShaderResourceType::eSampler:
					{
						SamplerBindingInfo samplerInfo{};
						samplerInfo.bindingID = bindingID;
						samplerInfo.samplerBindingName = binding.m_Name;
						samplerInfo.samplerCount = binding.m_ElementCount;
						spaceResourceInfo.samplerInfos.push_back(samplerInfo);
						spaceResourceInfo.GetStruct(structName).samplerRefs.push_back(spaceResourceInfo.samplerInfos.size() - 1);
						break;
					}
					}
				}
			}
		}
	}


	void D3D12ShaderResourceImporter::ImportResource(ResourceManagingSystem* resourceManager
		, cafs::path const& sourcePath
		, cafs::path const& destPath)
	{
		if (!cafs::exists(sourcePath))
		{
			return;
		}
		castl::cout << CATypeDescriptor<ShaderLibrary>::member_count << castl::endl;
		castl::unordered_map<cahash::sha256_hash::result_type, ShaderCode> shaderPrograms;

		cafs::path shaderLibraryPath = "D3D12ShaderLibrary.shLib";

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
					pCompiler->SetTarget(ShaderCompilerSlang::EShaderTargetType::eDXIL);
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
							if (result.targetType == ShaderCompilerSlang::EShaderTargetType::eDXIL)
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
										//shaderCode.data = program.data;
										careflection::managed_wrapper_traits<ComPtr<ID3DBlob>>::set_data(shaderCode.data, program.data);
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