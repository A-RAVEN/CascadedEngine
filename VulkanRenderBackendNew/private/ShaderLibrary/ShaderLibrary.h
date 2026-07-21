#pragma once
#include <CAResource/IResource.h>
#include <Hasher.h>
#include <Compiler.h>
#include <Utils/VulkanIncludes.h>

namespace graphics_backend
{
	// Task 1.1: Serializable descriptor binding (replaces vk::DescriptorSetLayoutBinding for reflection compatibility)
	struct VulkanDescriptorBinding
	{
		uint32_t binding;
		uint32_t descriptorType;  // vk::DescriptorType stored as uint32_t
		uint32_t descriptorCount;
		uint32_t stageFlags;      // vk::ShaderStageFlags stored as uint32_t

		vk::DescriptorSetLayoutBinding ToVulkan() const
		{
			vk::DescriptorSetLayoutBinding result{};
			result.binding = binding;
			result.descriptorType = static_cast<vk::DescriptorType>(descriptorType);
			result.descriptorCount = descriptorCount;
			result.stageFlags = vk::ShaderStageFlags(stageFlags);
			result.pImmutableSamplers = nullptr;
			return result;
		}

		static VulkanDescriptorBinding FromVulkan(vk::DescriptorSetLayoutBinding const& vkBinding)
		{
			return VulkanDescriptorBinding{
				vkBinding.binding,
				static_cast<uint32_t>(vkBinding.descriptorType),
				vkBinding.descriptorCount,
				static_cast<uint32_t>(vkBinding.stageFlags)
			};
		}
	};

	// Task 1.1: Vulkan descriptor set layout info
	// Task 4.14 [P]: Fixed pBindings lifetime issue - use GetCreateInfo() to rebuild on demand
	struct VulkanDescriptorSetLayoutInfo
	{
		uint32_t setIndex;
		castl::vector<VulkanDescriptorBinding> bindings;

		// Rebuild createInfo on demand - ensures pBindings always points to valid memory
		// Call this immediately before creating DescriptorSetLayout
		// outBindings must outlive the returned createInfo
		vk::DescriptorSetLayoutCreateInfo GetCreateInfo(castl::vector<vk::DescriptorSetLayoutBinding>& outBindings) const
		{
			outBindings.clear();
			outBindings.reserve(bindings.size());
			for (auto& b : bindings)
			{
				outBindings.push_back(b.ToVulkan());
			}
			return vk::DescriptorSetLayoutCreateInfo{}
				.setBindingCount(static_cast<uint32_t>(outBindings.size()))
				.setPBindings(outBindings.data());
		}

		size_t GetHash() const
		{
			size_t h = 0;
			cacore::hash_combine(h, setIndex);
			for (auto const& b : bindings)
			{
				cacore::hash_combine(h, b.binding);
				cacore::hash_combine(h, b.descriptorType);
				cacore::hash_combine(h, b.descriptorCount);
				cacore::hash_combine(h, b.stageFlags);
			}
			return h;
		}
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
		EShaderTypeFlags GetShaderStageUsage(uint32_t usageMask) const;
		auto operator<=>(const VulkanShaderFileInfo&) const = default;
	};

	// Task 1.6: Vulkan shader code (serializable, no runtime vk::ShaderModule)
	struct VulkanShaderCode
	{
		ECompileShaderType shaderType;
		castl::vector<uint32_t> spirvCode;
	};

	// NOTE: ShaderLibrary internal maps (m_ShaderPrograms, m_ShaderFiles, m_ShaderStructs,
	// m_ShaderRootStructs) are NOT thread-safe. ImportResource clears and repopulates these
	// maps during initialization. Do NOT access ShaderLibrary from multiple threads
	// concurrently. Any future hot-reload or background-compilation feature MUST add
	// synchronization before concurrent access.
	class ShaderLibrary : public resource_management::TResource<ShaderLibrary>
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

		friend struct CATypeDescriptor<ShaderLibrary>;
	};
}

CA_REFLECTION(graphics_backend::ShaderLibrary
	, m_ShaderFiles
	, m_ShaderPrograms
	, m_ShaderStructs
	, m_ShaderRootStructs);
