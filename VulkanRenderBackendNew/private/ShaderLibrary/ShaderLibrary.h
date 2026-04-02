#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <Hasher.h>
#include <PipelineStates/ShaderModule.h>
#include <Compiler.h>

namespace graphics_backend
{
	// Task 1.1: Vulkan descriptor set layout info
	struct VulkanDescriptorSetLayoutInfo
	{
		uint32_t setIndex;
		castl::vector<vk::DescriptorSetLayoutBinding> bindings;
		vk::DescriptorSetLayoutCreateInfo createInfo;
	};

	// Task 1.2: Vulkan binding info types (mirrors D3D12 CBufferBindingInfo)
	struct VulkanCBufferBindingInfo
	{
		uint32_t spaceID;
		uint32_t elementCount;
		uint32_t bindingID;
		uint32_t usageMask;
		cacore::NameHash cbufferStructName;
	};

	// Task 1.2: Vulkan image binding info
	struct VulkanImageBindingInfo
	{
		ShaderCompilerSlang::EShaderResourceType resourceType;
		ShaderCompilerSlang::EShaderResourceAccess accessType;
		uint32_t spaceID;
		uint32_t elementCount;
		uint32_t bindingID;
		uint32_t usageMask;
		cacore::NameHash imageBindingName;
		bool isUAV() const
		{
			return (resourceType == ShaderCompilerSlang::EShaderResourceType::eRWTexture) ||
				(resourceType == ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer);
		}
	};

	// Task 1.2: Vulkan buffer binding info
	struct VulkanBufferBindingInfo
	{
		ShaderCompilerSlang::EShaderResourceType resourceType;
		ShaderCompilerSlang::EShaderResourceAccess accessType;
		uint32_t spaceID;
		uint32_t elementCount;
		uint32_t bindingID;
		uint32_t usageMask;
		cacore::NameHash bufferBindingName;
		bool isUAV() const
		{
			return (resourceType == ShaderCompilerSlang::EShaderResourceType::eRWTexture) ||
				(resourceType == ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer);
		}
	};

	// Task 1.2: Vulkan sampler binding info
	struct VulkanSamplerBindingInfo
	{
		uint32_t spaceID;
		uint32_t elementCount;
		uint32_t bindingID;
		uint32_t usageMask;
		cacore::NameHash samplerBindingName;
	};

	// Task 1.3: Vulkan struct binding info with sub-struct references
	struct VulkanStructBindingInfo
	{
		static VulkanStructBindingInfo Create(cacore::NameHash const& name, uint32_t elementCount)
		{
			VulkanStructBindingInfo result{};
			result.structBindingName = name;
			result.elementCount = elementCount;
			result.subStructOffset = 0;
			result.subStructCount = 0;
			return result;
		}
		void InitSubStructs(uint32_t offset, uint32_t count)
		{
			subStructOffset = offset;
			subStructCount = count;
		}
		uint32_t elementCount;
		uint32_t subStructOffset;
		uint32_t subStructCount;
		cacore::NameHash structBindingName;
		castl::vector<uint32_t> cbufferRefs;
		castl::vector<uint32_t> imageRefs;
		castl::vector<uint32_t> bufferRefs;
		castl::vector<uint32_t> samplerRefs;
	};

	// Task 1.4: Aggregating all binding info types
	struct VulkanShaderResourceBindingInfo
	{
		uint32_t totalDescriptorCount;
		uint32_t samplerDescriptorCount;
		castl::vector<VulkanDescriptorSetLayoutInfo> setLayoutInfos;
		castl::vector<VulkanCBufferBindingInfo> cbufferInfos;
		castl::vector<VulkanImageBindingInfo> imageInfos;
		castl::vector<VulkanBufferBindingInfo> bufferInfos;
		castl::vector<VulkanSamplerBindingInfo> samplerInfos;
		castl::vector<VulkanStructBindingInfo> structBindingInfos;

		void EmplaceStruct(cacore::NameHash const& name, uint32_t count)
		{
			structBindingInfos.push_back(VulkanStructBindingInfo::Create(name, count));
		}
	};

	// Task 1.5: Vulkan shader file info
	struct VulkanShaderFileInfo
	{
		struct ProgramInfo
		{
			cacore::NameHash entryPointName;
			cahash::sha256_hash::result_type programHash;
			ECompileShaderType shaderType;
		};
		cacore::PathHash path;
		castl::vector<ProgramInfo> entryPointToShaderProgram;
		ShaderCompilerSlang::ShaderReflectionData reflectionData;
		VulkanShaderResourceBindingInfo shaderBindingInfo;
		auto operator<=>(const VulkanShaderFileInfo&) const = default;
	};

	// Task 1.6: Vulkan shader code
	struct VulkanShaderCode
	{
		ECompileShaderType shaderType;
		vk::UniqueShaderModule shaderModule;
		castl::vector<uint32_t> spirvCode;
	};

	struct ShaderCodeSource
	{
		uint32_t shaderCodeLength;
		void* pShaderCode;
		VKShaderCodeHashVal shaderCodeHash;
	};

	class ShaderLibrary : public VulkanSubobjectBase
	{
	public:
		castl::unordered_map<cacore::PathHash, VulkanShaderFileInfo> m_ShaderFiles;
		castl::unordered_map<cahash::sha256_hash::result_type, VulkanShaderCode> m_ShaderPrograms;
		castl::unordered_map<cacore::NameHash, ShaderCompilerSlang::ShaderStructData> m_ShaderStructs;
		castl::unordered_map<cacore::PathHash, ShaderCompilerSlang::ShaderStructData> m_ShaderRootStructs;

		VulkanShaderFileInfo const* GetShaderFileInfo(cacore::PathHash const& pathHash) const;
		VulkanShaderCode const* GetShaderCode(cahash::sha256_hash::result_type const& shaHash) const;
		ShaderCompilerSlang::ShaderStructData const* GetShaderStruct(cacore::NameHash const& nameHash) const;
		ShaderCompilerSlang::ShaderStructData const* GetShaderRootStruct(cacore::PathHash const& pathHash) const;

		bool TryAquireShaderModule(ShaderModuleCache const& cache
			, TypedVKHashVal<ShaderModuleCache>& outShaderModuleCache);
		bool ShaderModuleCacheValid(TypedVKHashVal<ShaderModuleCache> const& cache, VKShaderCodeHashVal const& shaderCodeHash) const;
		ShaderModuleCache GetShaderModuleCache(TypedVKHashVal<ShaderModuleCache> const& cache) const;
		ShaderCodeSource GetShaderCodeSource(TypedVKHashVal<ShaderModuleCache> const& cache) const;
	};
}
