#include "ShaderImporter_D12.h"
#include <CAResource/ResourceManagingSystem.h>
#include <CASTL/CAUnorderedSet.h>
#include <CAResource/IResource.h>
#include "ShaderLibrary.h"
#include <CASTL/CADeque.h>
#include <D3D12Debug.h>

namespace graphics_backend
{
	

	struct HierarchyElement
	{
		ShaderCompilerSlang::ShaderBindingHierarchy const* pHierarchy;
		uint32_t hierarchyID;
		uint32_t offset;
	};

	void IterateHierarchyElements(ShaderCompilerSlang::ShaderReflectionData const* reflectionData
		, castl::function<void(HierarchyElement const&)> hierarchyElementCallback)
	{
		using namespace ShaderCompilerSlang;
		auto& bindingInfo = reflectionData->m_BindingInfo;
		auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];

		castl::deque<HierarchyElement> hierarchies;
		hierarchies.push_back(HierarchyElement{ &rootHierarchy, (uint32_t)bindingInfo.m_RootHierarchyID , 0 });
		while (!hierarchies.empty())
		{
			HierarchyElement bounds = hierarchies.front();
			hierarchyElementCallback(bounds);
			hierarchies.pop_front();
			CA_ASSERT_BREAK(bounds.pHierarchy != nullptr, "Hierarchy Should Never Be Null");
			{
				auto& itrHierarchy = *bounds.pHierarchy;
				uint32_t offset = bounds.offset;
				for (auto subHierarchyID : itrHierarchy.m_SubBindingHierarchies)
				{
					auto& subHierarchy = bindingInfo.m_BindingDataHierarchies[subHierarchyID];
		
					for (uint32_t id = 0; id < subHierarchy.m_ElementCount; ++id)
					{
						uint32_t resolvedOffset = id + offset * subHierarchy.m_ElementCount;
						hierarchies.push_back(HierarchyElement{ &subHierarchy , (uint32_t)subHierarchyID, resolvedOffset });
					}
				}
			}
		}
	}


	ShaderResourceBindingInfo ConstructShaderDescriptorInfo(ShaderCompilerSlang::ShaderReflectionData const& shaderReflectionData)
	{
		auto& spaceInfos = shaderReflectionData.m_BindingInfo.m_SpaceInfos;
		ShaderResourceBindingInfo resourceBindingInfo;
		resourceBindingInfo.resourceDescCount = 0;
		resourceBindingInfo.samplerDescCount = 0;
		resourceBindingInfo.resourceHeapParamID = -1;
		resourceBindingInfo.samplerHeapParamID = -1;
		auto& bindingInfo = shaderReflectionData.m_BindingInfo;

		std::vector<D3D12_DESCRIPTOR_RANGE1> descriptorRanges;
		std::vector<D3D12_DESCRIPTOR_RANGE1> samplerDescriptorRanges;

		{
			castl::deque<int> hierarchyIDs;
			hierarchyIDs.push_back(bindingInfo.m_RootHierarchyID);
			resourceBindingInfo.structBindingInfos.reserve(bindingInfo.m_BindingDataHierarchies.size());
			{
				auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];
				resourceBindingInfo.EmplaceStruct(rootHierarchy.m_Name, 1);
			}

			size_t counter = 0;
			while (counter != hierarchyIDs.size())
			{
				uint32_t hierarchyID = hierarchyIDs[counter];
				auto& processingHierarchy = bindingInfo.m_BindingDataHierarchies[hierarchyID];
				auto& currentStructBindingInfo = resourceBindingInfo.structBindingInfos[counter];
				CA_ASSERT_BREAK(processingHierarchy.m_Name == currentStructBindingInfo.structBindingName, "Struct Binding Name Incompatible");
				auto& structName = processingHierarchy.m_Name;
				//add child binding infos
				currentStructBindingInfo.InitSubStructs(resourceBindingInfo.structBindingInfos.size()
					, processingHierarchy.m_SubBindingHierarchies.size());
				for (auto& subHierarchyID : processingHierarchy.m_SubBindingHierarchies)
				{
					auto& subHierarchy = bindingInfo.m_BindingDataHierarchies[subHierarchyID];
					//TODO: check if this hierarchy has any useful data for this space
					hierarchyIDs.push_back(subHierarchyID);
					resourceBindingInfo.EmplaceStruct(subHierarchy.m_Name
						, subHierarchy.m_ElementCount * currentStructBindingInfo.elementCount);
				}

				uint32_t currentStructElementCount = currentStructBindingInfo.elementCount;

				//Collect Uniform Buffer
				if (processingHierarchy.m_SelfUniformBufferID != -1)
				{
					CA_ASSERT_BREAK(processingHierarchy.m_SelfUniformSpaceID != -1
						, "invalid uniform space id: {}"
						, processingHierarchy.m_SelfUniformSpaceID);

					CBufferBindingInfo cbufferInfo{};
					cbufferInfo.elementCount = currentStructElementCount;
					cbufferInfo.spaceID = processingHierarchy.m_SelfUniformSpaceID;
					cbufferInfo.bindingID = processingHierarchy.m_SelfUniformBufferID;
					cbufferInfo.usageMask = processingHierarchy.m_SelfUniformUsage;
					cbufferInfo.cbufferStructName = structName;
					cbufferInfo.descTableID = resourceBindingInfo.resourceDescCount;
					currentStructBindingInfo.cbufferRefs.push_back(resourceBindingInfo.cbufferInfos.size());
					resourceBindingInfo.cbufferInfos.push_back(cbufferInfo);

					CA_LOG("cbuffer info[{}], bindingID[{}],spaceID[{}],elementCount[{}],descTableID[{}]",
						processingHierarchy.m_Name
						, processingHierarchy.m_SelfUniformBufferID
						, processingHierarchy.m_SelfUniformSpaceID
						, currentStructElementCount, resourceBindingInfo.resourceDescCount);

					CD3DX12_DESCRIPTOR_RANGE1 range;
					range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV
						, cbufferInfo.elementCount
						, cbufferInfo.bindingID, cbufferInfo.spaceID
						, D3D12_DESCRIPTOR_RANGE_FLAG_NONE
						, resourceBindingInfo.resourceDescCount);
					descriptorRanges.push_back(range);
					resourceBindingInfo.resourceDescCount += cbufferInfo.elementCount;
				}

				//Collect Resources
				if (!processingHierarchy.m_Bindings.empty())
				{
					for (auto& binding : processingHierarchy.m_Bindings)
					{
						auto bindingID = binding.m_BindingID;
						auto bindingSpace = binding.m_BindingSpace;
						uint32_t elementCount = currentStructElementCount * binding.m_ElementCount;
						//Collect For Binding Info
						switch (binding.m_ResourceType)
						{
						case ShaderCompilerSlang::EShaderResourceType::eTexture:
						case ShaderCompilerSlang::EShaderResourceType::eRWTexture:
						{
							CA_LOG("image info[{}], bindingID[{}],spaceID[{}],elementCount[{}],descTableID[{}]",
								binding.m_Name, bindingID, bindingSpace, elementCount, resourceBindingInfo.resourceDescCount);
							ImageBindingInfo imageInfo{};
							imageInfo.accessType = binding.m_Access;
							imageInfo.resourceType = binding.m_ResourceType;
							imageInfo.spaceID = bindingSpace;
							imageInfo.bindingID = bindingID;
							imageInfo.usageMask = binding.m_Usage;
							imageInfo.imageBindingName = binding.m_Name;
							imageInfo.elementCount = elementCount;
							imageInfo.descTableID = resourceBindingInfo.resourceDescCount;
							currentStructBindingInfo.imageRefs.push_back(resourceBindingInfo.imageInfo.size());
							resourceBindingInfo.imageInfo.push_back(imageInfo);
							break;
						}
						case ShaderCompilerSlang::EShaderResourceType::eStructuredBuffer:
						case ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer:
						{
							CA_LOG("buffer info[{}], bindingID[{}],spaceID[{}],elementCount[{}],descTableID[{}]",
								binding.m_Name, bindingID, bindingSpace, elementCount, resourceBindingInfo.resourceDescCount);
							BufferBindingInfo bufferInfo{};
							bufferInfo.accessType = binding.m_Access;
							bufferInfo.resourceType = binding.m_ResourceType;
							bufferInfo.spaceID = bindingSpace;
							bufferInfo.bindingID = bindingID;
							bufferInfo.usageMask = binding.m_Usage;
							bufferInfo.bufferBindingName = binding.m_Name;
							bufferInfo.elementCount = elementCount;
							bufferInfo.descTableID = resourceBindingInfo.resourceDescCount;
							currentStructBindingInfo.bufferRefs.push_back(resourceBindingInfo.bufferInfos.size());
							resourceBindingInfo.bufferInfos.push_back(bufferInfo);
							break;
						}
						case ShaderCompilerSlang::EShaderResourceType::eSampler:
						{
							SamplerBindingInfo samplerInfo{};
							samplerInfo.spaceID = bindingSpace;
							samplerInfo.bindingID = bindingID;
							samplerInfo.usageMask = binding.m_Usage;
							samplerInfo.samplerBindingName = binding.m_Name;
							samplerInfo.elementCount = elementCount;
							samplerInfo.descTableID = resourceBindingInfo.samplerDescCount;
							currentStructBindingInfo.samplerRefs.push_back(resourceBindingInfo.samplerInfos.size());
							resourceBindingInfo.samplerInfos.push_back(samplerInfo);
							break;
						}
						}

						//Collect For Resource Table Descriptor
						CD3DX12_DESCRIPTOR_RANGE1 range;
						switch (binding.m_ResourceType)
						{
						case ShaderCompilerSlang::EShaderResourceType::eRWTexture:
						case ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer:
							range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV
								, elementCount
								, bindingID
								, bindingSpace
								, D3D12_DESCRIPTOR_RANGE_FLAG_NONE
								, resourceBindingInfo.resourceDescCount);
							descriptorRanges.push_back(range);
							resourceBindingInfo.resourceDescCount += elementCount;
							break;
						case ShaderCompilerSlang::EShaderResourceType::eTexture:
						case ShaderCompilerSlang::EShaderResourceType::eStructuredBuffer:
							range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV
								, elementCount
								, bindingID
								, bindingSpace
								, D3D12_DESCRIPTOR_RANGE_FLAG_NONE
								, resourceBindingInfo.resourceDescCount);
							descriptorRanges.push_back(range);
							resourceBindingInfo.resourceDescCount += elementCount;
							break;
						case ShaderCompilerSlang::EShaderResourceType::eSampler:
							range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
								, elementCount
								, bindingID
								, bindingSpace
								, D3D12_DESCRIPTOR_RANGE_FLAG_NONE
								, resourceBindingInfo.samplerDescCount);
							samplerDescriptorRanges.push_back(range);
							resourceBindingInfo.samplerDescCount += elementCount;
							break;
						}
					}
				}

				++counter;
			}
		}

		if (!descriptorRanges.empty())
		{
			uint32_t descCount = (descriptorRanges.back().NumDescriptors + descriptorRanges.back().OffsetInDescriptorsFromTableStart);
			CA_ASSERT_BREAK(
				resourceBindingInfo.resourceDescCount == descCount
				, "Incompatible Resource Desc Count:[{}/{}]", resourceBindingInfo.resourceDescCount, descCount);
			if (descriptorRanges.size() > 1)
			{
				for (int id = 0; id < descriptorRanges.size() - 1; ++id)
				{
					auto& thisRange = descriptorRanges[id];
					auto& nextRange = descriptorRanges[id + 1];
					CA_ASSERT_BREAK(
						(thisRange.OffsetInDescriptorsFromTableStart + thisRange.NumDescriptors) == nextRange.OffsetInDescriptorsFromTableStart
						, "Incompatible Resource Range Neighbours:[{}/{}/{}]"
						, thisRange.OffsetInDescriptorsFromTableStart
						, thisRange.NumDescriptors
						, nextRange.OffsetInDescriptorsFromTableStart);
				}
			}
		}
		if (!samplerDescriptorRanges.empty())
		{
			uint32_t descCount = (samplerDescriptorRanges.back().NumDescriptors + samplerDescriptorRanges.back().OffsetInDescriptorsFromTableStart);
			CA_ASSERT_BREAK(
				resourceBindingInfo.samplerDescCount == descCount
				, "Incompatible Sampler Desc Count:[{}/{}]", resourceBindingInfo.samplerDescCount, descCount);
			if (samplerDescriptorRanges.size() > 1)
			{
				for (int id = 0; id < samplerDescriptorRanges.size() - 1; ++id)
				{
					auto& thisRange = samplerDescriptorRanges[id];
					auto& nextRange = samplerDescriptorRanges[id + 1];
					CA_ASSERT_BREAK(
						(thisRange.OffsetInDescriptorsFromTableStart + thisRange.NumDescriptors) == nextRange.OffsetInDescriptorsFromTableStart
						, "Incompatible Sampler Range Neighbours:[{}/{}/{}]"
						, thisRange.OffsetInDescriptorsFromTableStart
						, thisRange.NumDescriptors
						, nextRange.OffsetInDescriptorsFromTableStart);
				}
			}
		}
		
		castl::vector<D3D12_ROOT_PARAMETER1> rootParameters;
		if (!descriptorRanges.empty())
		{
			resourceBindingInfo.resourceHeapParamID = rootParameters.size();
			CD3DX12_ROOT_PARAMETER1 resourceTableParams;
			resourceTableParams.InitAsDescriptorTable(descriptorRanges.size(), descriptorRanges.data());
			rootParameters.push_back(resourceTableParams);
		}
		if (!samplerDescriptorRanges.empty())
		{
			resourceBindingInfo.samplerHeapParamID = rootParameters.size();
			CD3DX12_ROOT_PARAMETER1 samplerTableParams;
			samplerTableParams.InitAsDescriptorTable(samplerDescriptorRanges.size(), samplerDescriptorRanges.data());
			rootParameters.push_back(samplerTableParams);
		}

		if (!rootParameters.empty())
		{
			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
			rootSigDesc.Init_1_1(rootParameters.size(), rootParameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			ComPtr<ID3DBlob> errorBlob = nullptr;
			// 编译root signature 描述结构
			ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSigDesc, resourceBindingInfo.serializedRootSignatureData.GetAddressOf(), errorBlob.GetAddressOf()));
			if (errorBlob != nullptr)
			{
				CA_LOG_ERR_BREAK("{}", (char*)errorBlob->GetBufferPointer());
			}
		}
		return resourceBindingInfo;
	}

	void D3D12ShaderResourceImporter::Test()
	{
		castl::string sourcePath = "E:/Projects/CascadedEngine/CAResources/Shaders";
		castl::string filePath = "E:/Projects/CascadedEngine/CAResources/Shaders/TestBindingShader.slang";
		auto pCompiler = m_ShaderCompilerManager->AquireShaderCompilerShared();
		pCompiler->BeginCompileTask();
		pCompiler->AddInlcudePath(sourcePath.c_str());
		pCompiler->AddSourceFile(filePath.c_str());
		pCompiler->EnableDebugInfo();
		pCompiler->SetTarget(ShaderCompilerSlang::EShaderTargetType::eSpirV);
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
	/*			if (!result.programs.empty())
				{
					castl::string str;
					str.resize(result.programs[0].data.size());
					memcpy(str.data(), result.programs[0].data.data(), result.programs[0].data.size());
					CA_LOG(str);
				}*/
				{
					ConstructShaderDescriptorInfo(result.m_ReflectionData);
				}
			}
		}
		pCompiler->EndCompileTask();
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
								shaderInfo.shaderBindingInfo = ConstructShaderDescriptorInfo(shaderInfo.reflectionData);
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
									shaderInfo.entryPointToShaderProgram.push_back({ entryPointName, shaHash, program.shaderType });
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