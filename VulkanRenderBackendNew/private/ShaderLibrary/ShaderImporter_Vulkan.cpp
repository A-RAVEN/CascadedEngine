#include <ShaderLibrary/ShaderImporter_Vulkan.h>
#include <RenderBackend_Vulkan.h>
#include <Utils/VulkanDebug.h>
#include <CASTL/CADeque.h>

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

	// Task 3.2-3.8: ConstructShaderDescriptorInfo implementation
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

		// Map to track bindings per set index for Task 3.7
		castl::unordered_map<uint32_t, castl::vector<vk::DescriptorSetLayoutBinding>> setBindings;

		// Task 3.2: Main hierarchy traversal following D3D12 pattern
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

				// Add child binding infos
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

				// Task 3.3: Collect Uniform Buffer (cbuffer) bindings
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

					// Task 3.8: Logging
					CA_LOG("cbuffer info[{}], bindingID[{}], spaceID[{}], elementCount[{}]",
						processingHierarchy.m_Name.Get()
						, processingHierarchy.m_SelfUniformBufferID
						, processingHierarchy.m_SelfUniformSpaceID
						, currentStructElementCount);

					// Task 3.7: Build DescriptorSetLayoutBinding for cbuffer
					vk::DescriptorSetLayoutBinding layoutBinding{};
					layoutBinding.binding = cbufferInfo.bindingID;
					layoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
					layoutBinding.descriptorCount = cbufferInfo.elementCount;
					layoutBinding.stageFlags = vk::ShaderStageFlagBits::eAllGraphics | vk::ShaderStageFlagBits::eCompute;
					layoutBinding.pImmutableSamplers = nullptr;
					setBindings[cbufferInfo.spaceID].push_back(layoutBinding);

					resourceBindingInfo.totalDescriptorCount += cbufferInfo.elementCount;
				}

				// Task 3.4-3.6: Collect Resources (image, buffer, sampler)
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

							// Task 3.7: Build DescriptorSetLayoutBinding for image
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

							// Task 3.7: Build DescriptorSetLayoutBinding for buffer
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

							// Task 3.7: Build DescriptorSetLayoutBinding for sampler
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

		// Task 3.7: Build VulkanDescriptorSetLayoutInfo per set index
		for (auto& [setIndex, bindings] : setBindings)
		{
			VulkanDescriptorSetLayoutInfo setLayoutInfo{};
			setLayoutInfo.setIndex = setIndex;
			setLayoutInfo.bindings = castl::move(bindings);
			setLayoutInfo.createInfo = vk::DescriptorSetLayoutCreateInfo{}
				.setBindingCount((uint32_t)setLayoutInfo.bindings.size())
				.setPBindings(setLayoutInfo.bindings.data());
			resourceBindingInfo.setLayoutInfos.push_back(castl::move(setLayoutInfo));
		}

		// Task 3.8: Summary logging
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
	void ShaderImporter_Vulkan::Init()
	{
		CA_LOG_INFO("ShaderImporter_Vulkan initialized");
	}

	void ShaderImporter_Vulkan::Release()
	{
		auto device = GetDevice();
		for (auto module : m_CreatedModules)
		{
			if (module)
			{
				device.destroyShaderModule(module);
			}
		}
		m_CreatedModules.clear();
		m_ShaderCompilerManager = nullptr;
		CA_LOG_INFO("ShaderImporter_Vulkan released");
	}

	// FR-013: SetCompiler method following D3D12ShaderResourceImporter pattern
	void ShaderImporter_Vulkan::SetCompiler(ShaderCompilerSlang::IShaderCompilerManager* compiler)
	{
		m_ShaderCompilerManager = compiler;
		CA_LOG_INFO("ShaderImporter_Vulkan: Shader compiler manager set");
	}

	// FR-013: Compile from source using IShaderCompilerManager
	bool ShaderImporter_Vulkan::CompileFromSource(
		castl::string const& sourcePath,
		castl::string const& entryPoint,
		vk::ShaderStageFlagBits stage,
		VulkanCompiledShaderInfo& outInfo)
	{
		if (!m_ShaderCompilerManager)
		{
			CA_LOG_ERR("ShaderImporter_Vulkan: No shader compiler manager set - call SetCompiler() first");
			return false;
		}

		// FR-013: Use IShaderCompilerManager for shader source compilation
		// Pattern: AquireShaderCompilerShared → BeginCompileTask → SetTarget(eSpirV) → Compile → GetResults
		auto pCompiler = m_ShaderCompilerManager->AquireShaderCompilerShared();
		if (!pCompiler)
		{
			CA_LOG_ERR("ShaderImporter_Vulkan: Failed to acquire shader compiler");
			return false;
		}

		pCompiler->BeginCompileTask();

		// Add source file
		pCompiler->AddSourceFile(sourcePath.c_str());

		// Add entry point if specified
		if (!entryPoint.empty())
		{
			ShaderCompilerSlang::ECompileShaderType shaderType;
			switch (stage)
			{
			case vk::ShaderStageFlagBits::eVertex:
				shaderType = ShaderCompilerSlang::ECompileShaderType::eVertex;
				break;
			case vk::ShaderStageFlagBits::eFragment:
				shaderType = ShaderCompilerSlang::ECompileShaderType::eFragment;
				break;
			case vk::ShaderStageFlagBits::eCompute:
				shaderType = ShaderCompilerSlang::ECompileShaderType::eCompute;
				break;
			case vk::ShaderStageFlagBits::eGeometry:
				shaderType = ShaderCompilerSlang::ECompileShaderType::eGeometry;
				break;
			default:
				shaderType = ShaderCompilerSlang::ECompileShaderType::eVertex;
				break;
			}
			pCompiler->AddEntryPoint(entryPoint.c_str(), shaderType);
		}

		// FR-013: Set target to SPIR-V for Vulkan
		pCompiler->SetTarget(ShaderCompilerSlang::EShaderTargetType::eSpirV);

		// Compile
		pCompiler->Compile();

		if (pCompiler->HasError())
		{
			CA_LOG_ERR("ShaderImporter_Vulkan: Shader compilation failed for {}", sourcePath);
			pCompiler->EndCompileTask();
			return false;
		}

		// FR-013: Extract reflection data from GetResults() → result.m_ReflectionData
		auto compileResults = pCompiler->GetResults();
		if (compileResults.empty())
		{
			CA_LOG_ERR("ShaderImporter_Vulkan: No compilation results for {}", sourcePath);
			pCompiler->EndCompileTask();
			return false;
		}

		// Find SPIR-V result
		bool foundSpirV = false;
		for (auto& result : compileResults)
		{
			if (result.targetType == ShaderCompilerSlang::EShaderTargetType::eSpirV)
			{
				foundSpirV = true;

				// Store reflection data (FR-013)
				outInfo.reflectionData = result.m_ReflectionData;

				// Find the program for our entry point
				for (auto& program : result.programs)
				{
					// Convert SPIR-V data to uint32_t vector
					size_t dataSize = program.data.size();
					if (dataSize % sizeof(uint32_t) != 0)
					{
						CA_LOG_WARN("ShaderImporter_Vulkan: SPIR-V data size not aligned to uint32_t");
					}

					outInfo.spirvCode.resize(dataSize / sizeof(uint32_t));
					memcpy(outInfo.spirvCode.data(), program.data.data(), dataSize);

					// Create shader module
					outInfo.shaderModule = CreateShaderModule(outInfo.spirvCode);
					if (!outInfo.shaderModule)
					{
						CA_LOG_ERR("ShaderImporter_Vulkan: Failed to create shader module");
						pCompiler->EndCompileTask();
						return false;
					}

					outInfo.stage = stage;
					outInfo.entryPoint = cacore::NameHash(program.entryPointName);

					// FR-013: Extract bindings from reflection data
					ExtractBindingsFromReflection(result.m_ReflectionData, outInfo);

					m_CreatedModules.push_back(outInfo.shaderModule);

					CA_LOG_INFO("ShaderImporter_Vulkan: Compiled shader {} entry {} for stage {}",
						sourcePath, program.entryPointName, (int)stage);

					break; // Use first matching program
				}
				break;
			}
		}

		pCompiler->EndCompileTask();

		if (!foundSpirV)
		{
			CA_LOG_ERR("ShaderImporter_Vulkan: No SPIR-V output found in compilation results");
			return false;
		}

		return true;
	}

	bool ShaderImporter_Vulkan::CompileFromSPIRV(
		vk::ShaderStageFlagBits stage,
		castl::vector<uint32_t> const& spirvCode,
		cacore::NameHash const& entryPoint,
		VulkanCompiledShaderInfo& outInfo)
	{
		if (spirvCode.empty())
		{
			CA_LOG_ERR("ShaderImporter_Vulkan: Empty SPIR-V code");
			return false;
		}

		// Create shader module
		outInfo.shaderModule = CreateShaderModule(spirvCode);
		if (!outInfo.shaderModule)
		{
			CA_LOG_ERR("ShaderImporter_Vulkan: Failed to create shader module");
			return false;
		}

		outInfo.stage = stage;
		outInfo.entryPoint = entryPoint;
		outInfo.spirvCode = spirvCode;

		// Note: For pre-compiled SPIR-V, we don't have reflection data
		// Descriptor bindings should be set manually or via SPIRV-Reflect
		CA_LOG_WARN("ShaderImporter_Vulkan: Using pre-compiled SPIR-V - no reflection data available");

		m_CreatedModules.push_back(outInfo.shaderModule);
		CA_LOG_INFO("ShaderImporter_Vulkan: Compiled SPIR-V shader for stage {}", (int)stage);
		return true;
	}

	// FR-013: Extract descriptor bindings from ShaderCompilerSlang reflection data
	void ShaderImporter_Vulkan::ExtractBindingsFromReflection(
		ShaderCompilerSlang::ShaderReflectionData const& reflectionData,
		VulkanCompiledShaderInfo& outInfo)
	{
		using namespace ShaderCompilerSlang;

		auto& bindingInfo = reflectionData.m_BindingInfo;
		auto& hierarchies = bindingInfo.m_BindingDataHierarchies;

		// Convert shader stage to flags
		vk::ShaderStageFlags stageFlags = outInfo.stage;

		// Iterate through all hierarchies to collect bindings
		for (auto& hierarchy : hierarchies)
		{
			// Collect uniform buffer (cbuffer) bindings
			if (hierarchy.m_SelfUniformBufferID != -1)
			{
				VulkanDescriptorBindingInfo binding{};
				binding.set = hierarchy.m_SelfUniformSpaceID;
				binding.binding = hierarchy.m_SelfUniformBufferID;
				binding.descriptorType = vk::DescriptorType::eUniformBuffer;
				binding.descriptorCount = hierarchy.m_ElementCount;
				binding.stageFlags = stageFlags;
				binding.name = hierarchy.m_Name;
				outInfo.descriptorBindings.push_back(binding);

				CA_LOG("ShaderImporter_Vulkan: CBuffer '{}' set={} binding={} count={}",
					hierarchy.m_Name.Get(), binding.set, binding.binding, binding.descriptorCount);
			}

			// Collect resource bindings
			for (auto& resourceBinding : hierarchy.m_Bindings)
			{
				VulkanDescriptorBindingInfo binding{};
				binding.set = resourceBinding.m_BindingSpace;
				binding.binding = resourceBinding.m_BindingID;
				binding.descriptorType = ConvertToDescriptorType(resourceBinding.m_ResourceType, resourceBinding.m_Access);
				binding.descriptorCount = resourceBinding.m_ElementCount;
				binding.stageFlags = stageFlags;
				binding.name = resourceBinding.m_Name;
				outInfo.descriptorBindings.push_back(binding);

				CA_LOG("ShaderImporter_Vulkan: Resource '{}' set={} binding={} type={} count={}",
					resourceBinding.m_Name.Get(), binding.set, binding.binding,
					(int)resourceBinding.m_ResourceType, binding.descriptorCount);
			}
		}

		// TODO: Extract push constants from reflection data when available
		// Currently push constants need to be set manually or extracted from m_ShaderStructs

		CA_LOG_INFO("ShaderImporter_Vulkan: Extracted {} descriptor bindings from reflection data",
			outInfo.descriptorBindings.size());
	}

	// Convert ShaderCompilerSlang resource type to Vulkan descriptor type
	vk::DescriptorType ShaderImporter_Vulkan::ConvertToDescriptorType(
		ShaderCompilerSlang::EShaderResourceType resourceType,
		ShaderCompilerSlang::EShaderResourceAccess access) const
	{
		using namespace ShaderCompilerSlang;

		switch (resourceType)
		{
		case EShaderResourceType::eTexture:
			return vk::DescriptorType::eSampledImage;
		case EShaderResourceType::eRWTexture:
			return vk::DescriptorType::eStorageImage;
		case EShaderResourceType::eSampler:
			return vk::DescriptorType::eSampler;
		case EShaderResourceType::eStructuredBuffer:
			return vk::DescriptorType::eStorageBuffer;
		case EShaderResourceType::eRWStructuredBuffer:
			return vk::DescriptorType::eStorageBuffer;
		case EShaderResourceType::eCBuffer:
			return vk::DescriptorType::eUniformBuffer;
		default:
			return vk::DescriptorType::eSampledImage;
		}
	}

	castl::vector<vk::DescriptorSetLayoutBinding> ShaderImporter_Vulkan::GetDescriptorSetLayoutBindings(
		VulkanCompiledShaderInfo const& shaderInfo,
		uint32_t setIndex) const
	{
		castl::vector<vk::DescriptorSetLayoutBinding> bindings;
		for (auto const& info : shaderInfo.descriptorBindings)
		{
			if (info.set == setIndex)
			{
				vk::DescriptorSetLayoutBinding binding{};
				binding.binding = info.binding;
				binding.descriptorType = info.descriptorType;
				binding.descriptorCount = info.descriptorCount;
				binding.stageFlags = info.stageFlags;
				binding.pImmutableSamplers = nullptr;
				bindings.push_back(binding);
			}
		}
		return bindings;
	}

	castl::vector<vk::PushConstantRange> ShaderImporter_Vulkan::GetPushConstantRanges(
		VulkanCompiledShaderInfo const& shaderInfo) const
	{
		castl::vector<vk::PushConstantRange> ranges;
		for (auto const& info : shaderInfo.pushConstants)
		{
			vk::PushConstantRange range{};
			range.offset = info.offset;
			range.size = info.size;
			range.stageFlags = info.stageFlags;
			ranges.push_back(range);
		}
		return ranges;
	}

	vk::ShaderModule ShaderImporter_Vulkan::CreateShaderModule(castl::vector<uint32_t> const& spirvCode)
	{
		vk::ShaderModuleCreateInfo createInfo{};
		createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
		createInfo.pCode = spirvCode.data();

		try
		{
			return GetDevice().createShaderModule(createInfo);
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("ShaderImporter_Vulkan: Failed to create shader module: {}", e.what());
			return nullptr;
		}
	}
}
