#include <ShaderLibrary/ShaderImporter_Vulkan.h>
#include <RenderBackend_Vulkan.h>
#include <Utils/VulkanDebug.h>
#include <CASTL/CADeque.h>

namespace graphics_backend
{
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

	// FR-013: Construct Vulkan shader resource binding info following ConstructShaderDescriptorInfo pattern
	VulkanShaderResourceBindingInfo ShaderImporter_Vulkan::ConstructShaderDescriptorInfo(
		ShaderCompilerSlang::ShaderReflectionData const& reflectionData)
	{
		using namespace ShaderCompilerSlang;

		VulkanShaderResourceBindingInfo bindingInfo;
		auto& shaderBindingInfo = reflectionData.m_BindingInfo;
		auto& hierarchies = shaderBindingInfo.m_BindingDataHierarchies;

		// Iterate through hierarchies (following D3D12 ConstructShaderDescriptorInfo pattern)
		castl::deque<int32_t> hierarchyIDs;
		hierarchyIDs.push_back(shaderBindingInfo.m_RootHierarchyID);

		size_t counter = 0;
		while (counter < hierarchyIDs.size())
		{
			int32_t hierarchyID = hierarchyIDs[counter];
			auto& processingHierarchy = hierarchies[hierarchyID];

			// Add sub-hierarchies to processing queue
			for (auto& subHierarchyID : processingHierarchy.m_SubBindingHierarchies)
			{
				hierarchyIDs.push_back(subHierarchyID);
			}

			// Collect uniform buffer (cbuffer)
			if (processingHierarchy.m_SelfUniformBufferID != -1)
			{
				VulkanShaderResourceBindingInfo::CBufferInfo cbufferInfo{};
				cbufferInfo.set = processingHierarchy.m_SelfUniformSpaceID;
				cbufferInfo.binding = processingHierarchy.m_SelfUniformBufferID;
				cbufferInfo.elementCount = processingHierarchy.m_ElementCount;
				cbufferInfo.name = processingHierarchy.m_Name;
				bindingInfo.cbufferInfos.push_back(cbufferInfo);
			}

			// Collect resource bindings
			for (auto& binding : processingHierarchy.m_Bindings)
			{
				switch (binding.m_ResourceType)
				{
				case EShaderResourceType::eTexture:
				case EShaderResourceType::eRWTexture:
				{
					VulkanShaderResourceBindingInfo::ImageInfo imageInfo{};
					imageInfo.set = binding.m_BindingSpace;
					imageInfo.binding = binding.m_BindingID;
					imageInfo.elementCount = binding.m_ElementCount;
					imageInfo.name = binding.m_Name;
					imageInfo.access = binding.m_Access;
					imageInfo.resourceType = binding.m_ResourceType;
					bindingInfo.imageInfos.push_back(imageInfo);
					break;
				}
				case EShaderResourceType::eStructuredBuffer:
				case EShaderResourceType::eRWStructuredBuffer:
				{
					VulkanShaderResourceBindingInfo::BufferInfo bufferInfo{};
					bufferInfo.set = binding.m_BindingSpace;
					bufferInfo.binding = binding.m_BindingID;
					bufferInfo.elementCount = binding.m_ElementCount;
					bufferInfo.name = binding.m_Name;
					bufferInfo.access = binding.m_Access;
					bufferInfo.resourceType = binding.m_ResourceType;
					bindingInfo.bufferInfos.push_back(bufferInfo);
					break;
				}
				case EShaderResourceType::eSampler:
				{
					VulkanShaderResourceBindingInfo::SamplerInfo samplerInfo{};
					samplerInfo.set = binding.m_BindingSpace;
					samplerInfo.binding = binding.m_BindingID;
					samplerInfo.elementCount = binding.m_ElementCount;
					samplerInfo.name = binding.m_Name;
					bindingInfo.samplerInfos.push_back(samplerInfo);
					break;
				}
				case EShaderResourceType::eCBuffer:
					// Already handled via m_SelfUniformBufferID
					break;
				}
			}

			++counter;
		}

		CA_LOG_INFO("ShaderImporter_Vulkan: Constructed binding info - CBuffers: {}, Images: {}, Buffers: {}, Samplers: {}",
			bindingInfo.cbufferInfos.size(), bindingInfo.imageInfos.size(),
			bindingInfo.bufferInfos.size(), bindingInfo.samplerInfos.size());

		return bindingInfo;
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
