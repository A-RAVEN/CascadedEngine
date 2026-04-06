#include <ShaderLibrary/ShaderImporter_Vulkan.h>
#include <CAResource/ResourceManagingSystem.h>
#include <CASTL/CAUnorderedSet.h>
#include <CASTL/CADeque.h>
#include <DebugUtils.h>

namespace graphics_backend
{
	// Task 3.1 [P]: Implement IterateHierarchyElements helper (reference D3D12)
	void IterateHierarchyElements(
		ShaderCompilerSlang::ShaderReflectionData const* reflectionData,
		castl::function<void(VulkanHierarchyElement const&)> hierarchyElementCallback)
	{
		using namespace ShaderCompilerSlang;
		auto& bindingInfo = reflectionData->m_BindingInfo;
		auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];

		castl::deque<VulkanHierarchyElement> hierarchies;
		hierarchies.push_back(VulkanHierarchyElement{ &rootHierarchy, (uint32_t)bindingInfo.m_RootHierarchyID, 0 });

		while (!hierarchies.empty())
		{
			VulkanHierarchyElement bounds = hierarchies.front();
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
						hierarchies.push_back(VulkanHierarchyElement{ &subHierarchy, (uint32_t)subHierarchyID, resolvedOffset });
					}
				}
			}
		}
	}

	// Task 3.2-3.8: ConstructShaderDescriptorInfo implementation (reference D3D12 ConstructShaderDescriptorInfo)
	VulkanShaderResourceBindingInfo ConstructShaderDescriptorInfo(
		const char* pathName,
		ShaderCompilerSlang::ShaderReflectionData const& shaderReflectionData)
	{
		using namespace ShaderCompilerSlang;

		VulkanShaderResourceBindingInfo resourceBindingInfo;
		resourceBindingInfo.totalDescriptorCount = 0;
		resourceBindingInfo.samplerDescriptorCount = 0;

		auto& bindingInfo = shaderReflectionData.m_BindingInfo;
		auto& hierarchies = bindingInfo.m_BindingDataHierarchies;

		// Map to track bindings per set index for building DescriptorSetLayoutInfo
		castl::unordered_map<uint32_t, castl::vector<vk::DescriptorSetLayoutBinding>> setBindings;

		// Main hierarchy traversal following D3D12 pattern
		{
			castl::deque<int32_t> hierarchyIDs;
			hierarchyIDs.push_back(bindingInfo.m_RootHierarchyID);
			resourceBindingInfo.structBindingInfos.reserve(hierarchies.size());

			{
				auto& rootHierarchy = hierarchies[bindingInfo.m_RootHierarchyID];
				resourceBindingInfo.EmplaceStruct(rootHierarchy.m_Name, 1);
			}

			size_t counter = 0;
			while (counter != hierarchyIDs.size())
			{
				uint32_t hierarchyID = hierarchyIDs[counter];
				auto& processingHierarchy = hierarchies[hierarchyID];
				auto& currentStructBindingInfo = resourceBindingInfo.structBindingInfos[counter];
				CA_ASSERT_BREAK(processingHierarchy.m_Name == currentStructBindingInfo.structBindingName, "Struct Binding Name Incompatible");
				auto& structName = processingHierarchy.m_Name;

				// Add child hierarchies
				currentStructBindingInfo.InitSubStructs(
					(uint32_t)resourceBindingInfo.structBindingInfos.size(),
					(uint32_t)processingHierarchy.m_SubBindingHierarchies.size());

				for (auto& subHierarchyID : processingHierarchy.m_SubBindingHierarchies)
				{
					auto& subHierarchy = hierarchies[subHierarchyID];
					hierarchyIDs.push_back(subHierarchyID);
					resourceBindingInfo.EmplaceStruct(subHierarchy.m_Name
						, subHierarchy.m_ElementCount * currentStructBindingInfo.elementCount);
				}

				uint32_t currentStructElementCount = currentStructBindingInfo.elementCount;

				// Collect Uniform Buffer (cbuffer) bindings
				if (processingHierarchy.m_SelfUniformBufferID != -1)
				{
					CA_ASSERT_BREAK(processingHierarchy.m_SelfUniformSpaceID != -1
						, "invalid uniform space id: {}"
						, processingHierarchy.m_SelfUniformSpaceID);

					VulkanCBufferBindingInfo cbufferInfo{};
					cbufferInfo.elementCount = currentStructElementCount;
					cbufferInfo.spaceID = processingHierarchy.m_SelfUniformSpaceID;
					cbufferInfo.bindingID = processingHierarchy.m_SelfUniformBufferID;
					cbufferInfo.usageMask = processingHierarchy.m_SelfUniformUsage;
					cbufferInfo.cbufferStructName = structName;
					currentStructBindingInfo.cbufferRefs.push_back((uint32_t)resourceBindingInfo.cbufferInfos.size());
					resourceBindingInfo.cbufferInfos.push_back(cbufferInfo);

					CA_LOG("cbuffer info[{}], bindingID[{}], spaceID[{}], elementCount[{}]",
						processingHierarchy.m_Name.Get()
						, processingHierarchy.m_SelfUniformBufferID
						, processingHierarchy.m_SelfUniformSpaceID
						, currentStructElementCount);

					// Build DescriptorSetLayoutBinding for cbuffer
					vk::DescriptorSetLayoutBinding layoutBinding{};
					layoutBinding.binding = cbufferInfo.bindingID;
					layoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
					layoutBinding.descriptorCount = cbufferInfo.elementCount;
					layoutBinding.stageFlags = vk::ShaderStageFlagBits::eAllGraphics | vk::ShaderStageFlagBits::eCompute;
					layoutBinding.pImmutableSamplers = nullptr;
					setBindings[cbufferInfo.spaceID].push_back(layoutBinding);

					resourceBindingInfo.totalDescriptorCount += cbufferInfo.elementCount;
				}

				// Collect Resources (image, buffer, sampler)
				if (!processingHierarchy.m_Bindings.empty())
				{
					for (auto& binding : processingHierarchy.m_Bindings)
					{
						auto bindingID = binding.m_BindingID;
						auto bindingSpace = binding.m_BindingSpace;
						uint32_t elementCount = currentStructElementCount * binding.m_ElementCount;

						switch (binding.m_ResourceType)
						{
						// Task 3.4: Image binding extraction
						case EShaderResourceType::eTexture:
						case EShaderResourceType::eRWTexture:
						{
							CA_LOG("image info[{}], bindingID[{}], spaceID[{}], elementCount[{}]",
								binding.m_Name.Get(), bindingID, bindingSpace, elementCount);

							VulkanImageBindingInfo imageInfo{};
							imageInfo.accessType = binding.m_Access;
							imageInfo.resourceType = binding.m_ResourceType;
							imageInfo.spaceID = bindingSpace;
							imageInfo.bindingID = bindingID;
							imageInfo.usageMask = binding.m_Usage;
							imageInfo.imageBindingName = binding.m_Name;
							imageInfo.elementCount = elementCount;
							currentStructBindingInfo.imageRefs.push_back((uint32_t)resourceBindingInfo.imageInfos.size());
							resourceBindingInfo.imageInfos.push_back(imageInfo);

							// Build DescriptorSetLayoutBinding for image
							vk::DescriptorSetLayoutBinding layoutBinding{};
							layoutBinding.binding = bindingID;
							layoutBinding.descriptorType = (binding.m_ResourceType == EShaderResourceType::eRWTexture)
								? vk::DescriptorType::eStorageImage
								: vk::DescriptorType::eSampledImage;
							layoutBinding.descriptorCount = elementCount;
							layoutBinding.stageFlags = vk::ShaderStageFlagBits::eAllGraphics | vk::ShaderStageFlagBits::eCompute;
							layoutBinding.pImmutableSamplers = nullptr;
							setBindings[bindingSpace].push_back(layoutBinding);

							resourceBindingInfo.totalDescriptorCount += elementCount;
							break;
						}
						// Task 3.5: Buffer binding extraction
						case EShaderResourceType::eStructuredBuffer:
						case EShaderResourceType::eRWStructuredBuffer:
						{
							CA_LOG("buffer info[{}], bindingID[{}], spaceID[{}], elementCount[{}]",
								binding.m_Name.Get(), bindingID, bindingSpace, elementCount);

							VulkanBufferBindingInfo bufferInfo{};
							bufferInfo.accessType = binding.m_Access;
							bufferInfo.resourceType = binding.m_ResourceType;
							bufferInfo.spaceID = bindingSpace;
							bufferInfo.bindingID = bindingID;
							bufferInfo.usageMask = binding.m_Usage;
							bufferInfo.bufferBindingName = binding.m_Name;
							bufferInfo.elementCount = elementCount;
							currentStructBindingInfo.bufferRefs.push_back((uint32_t)resourceBindingInfo.bufferInfos.size());
							resourceBindingInfo.bufferInfos.push_back(bufferInfo);

							// Build DescriptorSetLayoutBinding for buffer
							vk::DescriptorSetLayoutBinding layoutBinding{};
							layoutBinding.binding = bindingID;
							layoutBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
							layoutBinding.descriptorCount = elementCount;
							layoutBinding.stageFlags = vk::ShaderStageFlagBits::eAllGraphics | vk::ShaderStageFlagBits::eCompute;
							layoutBinding.pImmutableSamplers = nullptr;
							setBindings[bindingSpace].push_back(layoutBinding);

							resourceBindingInfo.totalDescriptorCount += elementCount;
							break;
						}
						// Task 3.6: Sampler binding extraction
						case EShaderResourceType::eSampler:
						{
							CA_LOG("sampler info[{}], bindingID[{}], spaceID[{}], elementCount[{}]",
								binding.m_Name.Get(), bindingID, bindingSpace, elementCount);

							VulkanSamplerBindingInfo samplerInfo{};
							samplerInfo.spaceID = bindingSpace;
							samplerInfo.bindingID = bindingID;
							samplerInfo.usageMask = binding.m_Usage;
							samplerInfo.samplerBindingName = binding.m_Name;
							samplerInfo.elementCount = elementCount;
							currentStructBindingInfo.samplerRefs.push_back((uint32_t)resourceBindingInfo.samplerInfos.size());
							resourceBindingInfo.samplerInfos.push_back(samplerInfo);

							// Build DescriptorSetLayoutBinding for sampler
							vk::DescriptorSetLayoutBinding layoutBinding{};
							layoutBinding.binding = bindingID;
							layoutBinding.descriptorType = vk::DescriptorType::eSampler;
							layoutBinding.descriptorCount = elementCount;
							layoutBinding.stageFlags = vk::ShaderStageFlagBits::eAllGraphics | vk::ShaderStageFlagBits::eCompute;
							layoutBinding.pImmutableSamplers = nullptr;
							setBindings[bindingSpace].push_back(layoutBinding);

							resourceBindingInfo.samplerDescriptorCount += elementCount;
							break;
						}
						case EShaderResourceType::eCBuffer:
							// Already handled via m_SelfUniformBufferID
							break;
						}
					}
				}

				++counter;
			}
		}

		// Build VulkanDescriptorSetLayoutInfo per set index
		// Task 4.14 [P]: No longer store createInfo - use GetCreateInfo() on demand
		for (auto& [setIndex, bindings] : setBindings)
		{
			VulkanDescriptorSetLayoutInfo setLayoutInfo{};
			setLayoutInfo.setIndex = setIndex;
			setLayoutInfo.bindings = castl::move(bindings);
			// createInfo is rebuilt on demand via GetCreateInfo() to avoid dangling pointer
			resourceBindingInfo.setLayoutInfos.push_back(castl::move(setLayoutInfo));
		}

		// Summary logging
		CA_LOG("ConstructShaderDescriptorInfo[{}]: totalDescriptors={}, samplerDescriptors={}, sets={}, cbuffers={}, images={}, buffers={}, samplers={}, structs={}",
			pathName,
			resourceBindingInfo.totalDescriptorCount,
			resourceBindingInfo.samplerDescriptorCount,
			resourceBindingInfo.setLayoutInfos.size(),
			resourceBindingInfo.cbufferInfos.size(),
			resourceBindingInfo.imageInfos.size(),
			resourceBindingInfo.bufferInfos.size(),
			resourceBindingInfo.samplerInfos.size(),
			resourceBindingInfo.structBindingInfos.size());

		return resourceBindingInfo;
	}

	// SetCompiler method following D3D12ShaderResourceImporter pattern
	void ShaderImporter_Vulkan::SetCompiler(ShaderCompilerSlang::IShaderCompilerManager* compiler)
	{
		m_ShaderCompilerManager = compiler;
		CA_LOG("ShaderImporter_Vulkan: Shader compiler manager set");
	}

	// Test method for debugging (following D3D12 pattern)
	void ShaderImporter_Vulkan::Test()
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
				ConstructShaderDescriptorInfo(filePath.c_str(), result.m_ReflectionData);
			}
		}
		pCompiler->EndCompileTask();
	}

	// Task 4.3-4.10: ImportResource implementation following D3D12 pattern
	void ShaderImporter_Vulkan::ImportResource(
		resource_management::ResourceManagingSystem* resourceManager,
		cafs::path const& sourcePath,
		cafs::path const& destPath)
	{
		// Task 4.4: Check if source path exists
		if (!cafs::exists(sourcePath))
		{
			CA_LOG("ShaderImporter_Vulkan: Source path does not exist: {}", sourcePath.generic_string());
			return;
		}

		// Task 4.6: Get or create ShaderLibrary resource
		cafs::path shaderLibraryPath = "VulkanShaderLibrary.shLib";
		auto shaderLibrary = resourceManager->GetOrNewResource<ShaderLibrary>(shaderLibraryPath);

		// Task 4.13 [P]: Clear all collections to avoid accumulating old data on re-import
		shaderLibrary->m_ShaderPrograms.clear();
		shaderLibrary->m_ShaderFiles.clear();
		shaderLibrary->m_ShaderStructs.clear();
		shaderLibrary->m_ShaderRootStructs.clear();

		// Task 4.4: Directory traversal using recursive_directory_iterator
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

					// Task 4.5: Set compile target to SPIR-V (not eDXIL like D3D12)
					pCompiler->SetTarget(ShaderCompilerSlang::EShaderTargetType::eSpirV);

					pCompiler->Compile();

					if (pCompiler->HasError())
					{
						CA_LOG_ERR("ShaderImporter_Vulkan: Shader compile failed for {}", p.path().generic_string());
					}
					else
					{
						// Task 4.7: PathHash for shader file lookup
						cacore::PathHash shaderPathHash = shaderpath;
						auto compileResults = pCompiler->GetResults();

						for (auto& result : compileResults)
						{
							// Task 4.5: Only process SPIR-V results
							if (result.targetType == ShaderCompilerSlang::EShaderTargetType::eSpirV)
							{
								// Task 4.7: Populate m_ShaderFiles[pathHash]
								auto& shaderInfo = shaderLibrary->m_ShaderFiles[shaderPathHash];
								shaderInfo.path = shaderPathHash;
								shaderInfo.entryPointToShaderProgram.clear();
								shaderInfo.reflectionData = result.m_ReflectionData;

								// Task 4.10: Call ConstructShaderDescriptorInfo to generate bindingInfo
								shaderInfo.shaderBindingInfo = ConstructShaderDescriptorInfo(
									p.path().generic_string().c_str(),
									shaderInfo.reflectionData);

								// Task 4.8: Populate m_ShaderPrograms[shaHash]
								for (auto& program : result.programs)
								{
									auto shaHash = cahash::getHash<cahash::sha256_hash>(
										program.data.data(),
										program.data.size());

									auto found = shaderLibrary->m_ShaderPrograms.find(shaHash);
									if (found == shaderLibrary->m_ShaderPrograms.end())
									{
										// Task 4.8: Create VulkanShaderCode with spirvCode and shaderType
										VulkanShaderCode shaderCode;
										shaderCode.shaderType = program.shaderType;

										// Convert SPIR-V data to uint32_t vector
										size_t dataSize = program.data.size();
										shaderCode.spirvCode.resize(dataSize / sizeof(uint32_t));
										memcpy(shaderCode.spirvCode.data(), program.data.data(), dataSize);

										found = shaderLibrary->m_ShaderPrograms.insert(
											castl::make_pair(shaHash, castl::move(shaderCode))).first;
									}

									// Task 4.7: Add entry point mapping
									cacore::NameHash entryPointName = program.entryPointName;
									VulkanShaderFileInfo::ProgramInfo programInfo{};
									programInfo.entryPointName = entryPointName;
									programInfo.programHash = shaHash;
									programInfo.shaderType = program.shaderType;
									shaderInfo.entryPointToShaderProgram.push_back(programInfo);
								}

								// Task 4.9: Populate m_ShaderStructs and m_ShaderRootStructs
								for (auto& pairs : result.m_ReflectionData.m_ShaderStructs)
								{
									auto& name = pairs.first;
									auto& shaderStruct = pairs.second;

									if (name == CANAME("__Root"))
									{
										// Root struct is keyed by path hash
										shaderLibrary->m_ShaderRootStructs.insert(
											castl::make_pair(shaderpath, shaderStruct));
										castl::cout << "Root Struct For " << shaderpath.generic_string() << castl::endl;
									}
									else if (shaderLibrary->m_ShaderStructs.find(name) == shaderLibrary->m_ShaderStructs.end())
									{
										// Named structs are keyed by name hash
										castl::cout << "Add Shader Struct: " << name.Get() << castl::endl;
										shaderLibrary->m_ShaderStructs.insert(castl::make_pair(name, shaderStruct));
									}
								}
							}
						}
					}
					pCompiler->EndCompileTask();
				}
			}
		}

		CA_LOG("ShaderImporter_Vulkan: Import complete. Files={}, Programs={}, Structs={}, RootStructs={}",
			shaderLibrary->m_ShaderFiles.size(),
			shaderLibrary->m_ShaderPrograms.size(),
			shaderLibrary->m_ShaderStructs.size(),
			shaderLibrary->m_ShaderRootStructs.size());
	}
}
