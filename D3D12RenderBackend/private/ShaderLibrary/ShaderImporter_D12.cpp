#include "ShaderImporter_D12.h"
#include <CAResource/ResourceManagingSystem.h>
#include <CASTL/CAUnorderedSet.h>
#include <CAResource/IResource.h>
#include "ShaderLibrary.h"
#include <CASTL/CADeque.h>
#include <D3D12Debug.h>

namespace graphics_backend
{
	struct CBufferBindingInfo
	{
		uint32_t bindingID;
		uint32_t descTableID;
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
		uint32_t descTableID;
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
		uint32_t descTableID;
		cacore::NameHash bufferBindingName;
		int bufferCount;
	};

	struct SamplerBindingInfo
	{
		uint32_t bindingID;
		uint32_t descTableID;
		cacore::NameHash samplerBindingName;
		int samplerCount;
	};

	struct StructBindingInfos
	{
		StructBindingInfos(cacore::NameHash const& name) : structBindingName(name)
			, subStructOffset(0)
			, subStructCount(0)
		{

		}
		void InitSubStructs(uint32_t offset, uint32_t count)
		{
			subStructOffset = offset;
			subStructCount = count;
		}
		cacore::NameHash structBindingName;
		castl::vector<uint32_t> cbufferRefs;
		castl::vector<uint32_t> imageRefs;
		castl::vector<uint32_t> bufferRefs;
		castl::vector<uint32_t> samplerRefs;
		uint32_t subStructOffset;
		uint32_t subStructCount;
	};

	struct ShaderResourceBindingInfo
	{
		uint32_t spaceID;
		castl::vector<StructBindingInfos> structBindingInfos;
		castl::vector<CBufferBindingInfo> cbufferInfos;
		castl::vector<ImageBindingInfo> imageInfo;
		castl::vector<BufferBindingInfo> bufferInfos;
		castl::vector<SamplerBindingInfo> samplerInfos;

		void EmplaceStruct(cacore::NameHash const& name)
		{
			structBindingInfos.emplace_back(name);
		}
	};

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


	void ConstructShaderDescriptorInfo(ShaderCompilerSlang::ShaderReflectionData const& shaderReflectionData)
	{
		ShaderCompilerSlang::ShaderReflectionData const* p_ReflectionData = &shaderReflectionData;

		auto& spaceInfos = p_ReflectionData->m_BindingInfo.m_SpaceInfos;
		castl::vector<ShaderResourceBindingInfo> resourceBindingInfos;
		castl::vector<uint8_t> serializedRootSignatureData;
		auto& bindingInfo = p_ReflectionData->m_BindingInfo;
		size_t spaceCount = spaceInfos.size();
		resourceBindingInfos.resize(spaceCount);
		for (int spaceID = 0; spaceID < spaceInfos.size(); ++spaceID)
		{
			resourceBindingInfos[spaceID].spaceID = spaceID;
			auto& spaceInfo = spaceInfos[spaceID];
			auto& spaceStats = spaceInfo.m_ResourceStats;
			resourceBindingInfos[spaceID].cbufferInfos.resize(spaceStats.m_CBufferBindings.size());
		}

		std::vector<D3D12_DESCRIPTOR_RANGE1> descriptorRanges;
		std::vector<D3D12_DESCRIPTOR_RANGE1> samplerDescriptorRanges;
		for (size_t spaceID = 0; spaceID < spaceCount; ++spaceID)
		{
			castl::deque<int> hierarchyIDs;
			hierarchyIDs.push_back(bindingInfo.m_RootHierarchyID);
			ShaderResourceBindingInfo& spaceResourceBindingInfo = resourceBindingInfos[spaceID];
			//auto& ranges = spaceResourceBindingInfo.descriptorRanges;
			spaceResourceBindingInfo.structBindingInfos.reserve(bindingInfo.m_BindingDataHierarchies.size());
			{
				auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];
				spaceResourceBindingInfo.EmplaceStruct(rootHierarchy.m_Name);
			}

			size_t counter = 0;
			while (counter != hierarchyIDs.size())
			{
				uint32_t hierarchyID = hierarchyIDs[counter];
				auto& processingHierarchy = bindingInfo.m_BindingDataHierarchies[hierarchyID];
				auto& currentStructBindingInfo = spaceResourceBindingInfo.structBindingInfos[counter];
				CA_ASSERT_BREAK(processingHierarchy.m_Name == currentStructBindingInfo.structBindingName, "Struct Binding Name Incompatible");
				auto& structName = processingHierarchy.m_Name;
				//add child binding infos
				for (auto& subHierarchyID : processingHierarchy.m_SubBindingHierarchies)
				{
					auto& subHierarchy = bindingInfo.m_BindingDataHierarchies[subHierarchyID];
					//TODO: check if this hierarchy has any useful data for this space
					hierarchyIDs.push_back(subHierarchyID);
					spaceResourceBindingInfo.EmplaceStruct(subHierarchy.m_Name);
				}

				//Collect Uniform Buffer
				if (processingHierarchy.m_SelfUniformBufferID != -1 && processingHierarchy.m_SelfUniformSpaceID == spaceID)
				{
					CA_ASSERT_BREAK(processingHierarchy.m_SelfUniformSpaceID != -1, "invalid uniform space id: {}"
						, processingHierarchy.m_SelfUniformSpaceID);
					auto uniformBufferID = processingHierarchy.m_SelfUniformSpaceID;
					auto& cbufferInfo = spaceResourceBindingInfo.cbufferInfos[uniformBufferID];
					cbufferInfo.bindingID = processingHierarchy.m_SelfUniformBufferID;
					cbufferInfo.cbufferStructName = structName;
					cbufferInfo.descTableID = descriptorRanges.size();

					CD3DX12_DESCRIPTOR_RANGE1 range;
					range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, cbufferInfo.bindingID, spaceID);
					descriptorRanges.push_back(range);
				}

				//Collect Resources
				if (!processingHierarchy.m_Bindings.empty())
				{
					for (auto& binding : processingHierarchy.m_Bindings)
					{
						if (binding.m_BindingSpace != spaceID)
							continue;
						auto bindingID = binding.m_BindingID;

						//Collect For Binding Info
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
							imageInfo.descTableID = descriptorRanges.size();
							spaceResourceBindingInfo.imageInfo.push_back(imageInfo);
							currentStructBindingInfo.imageRefs.push_back(spaceResourceBindingInfo.imageInfo.size() - 1);
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
							bufferInfo.descTableID = descriptorRanges.size();
							spaceResourceBindingInfo.bufferInfos.push_back(bufferInfo);
							currentStructBindingInfo.bufferRefs.push_back(spaceResourceBindingInfo.bufferInfos.size() - 1);
							break;
						}
						case ShaderCompilerSlang::EShaderResourceType::eSampler:
						{
							SamplerBindingInfo samplerInfo{};
							samplerInfo.bindingID = bindingID;
							samplerInfo.samplerBindingName = binding.m_Name;
							samplerInfo.samplerCount = binding.m_ElementCount;
							samplerInfo.descTableID = samplerDescriptorRanges.size();
							spaceResourceBindingInfo.samplerInfos.push_back(samplerInfo);
							currentStructBindingInfo.samplerRefs.push_back(spaceResourceBindingInfo.samplerInfos.size() - 1);
							break;
						}
						}

						//Collect For Resource Table Descriptor
						switch (binding.m_ResourceType)
						{
						case ShaderCompilerSlang::EShaderResourceType::eRWTexture:
						case ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer:
							CD3DX12_DESCRIPTOR_RANGE1 range;
							range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, bindingID, spaceID);
							descriptorRanges.push_back(range);
							break;
						case ShaderCompilerSlang::EShaderResourceType::eTexture:
						case ShaderCompilerSlang::EShaderResourceType::eStructuredBuffer:
							CD3DX12_DESCRIPTOR_RANGE1 range;
							range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, bindingID, spaceID);
							descriptorRanges.push_back(range);
							break;
						case ShaderCompilerSlang::EShaderResourceType::eSampler:
							CD3DX12_DESCRIPTOR_RANGE1 range;
							range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, bindingID, spaceID);
							samplerDescriptorRanges.push_back(range);
							break;
						}
					}
				}

				++counter;
			}
		}
		
		castl::vector<D3D12_ROOT_PARAMETER1> rootParameters;
		if (!descriptorRanges.empty())
		{
			CD3DX12_ROOT_PARAMETER1 resourceTableParams;
			resourceTableParams.InitAsDescriptorTable(descriptorRanges.size(), descriptorRanges.data());
			rootParameters.push_back(resourceTableParams);
		}
		if (!samplerDescriptorRanges.empty())
		{
			CD3DX12_ROOT_PARAMETER1 samplerTableParams;
			samplerTableParams.InitAsDescriptorTable(samplerDescriptorRanges.size(), samplerDescriptorRanges.data());
			rootParameters.push_back(samplerTableParams);
		}

		if (!rootParameters.empty())
		{
			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
			rootSigDesc.Init_1_1(rootParameters.size(), rootParameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			ComPtr<ID3DBlob> serializedRootSig = nullptr;
			ComPtr<ID3DBlob> errorBlob = nullptr;
			// 编译root signature 描述结构
			ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSigDesc, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
			if (errorBlob != nullptr)
			{
				CA_LOG_ERR_BREAK("{}", (char*)errorBlob->GetBufferPointer());
			}
			serializedRootSignatureData.resize(serializedRootSig->GetBufferSize());
			memcpy(serializedRootSignatureData.data(), serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize());
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