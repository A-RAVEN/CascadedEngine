#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>
#include <CASTL/CADeque.h>
#include <CACore/CAHash.h>
#include <vk_mem_alloc.h>
#include <Interface/ShaderCompiler/header/Compiler.h>
#include <ShaderLibrary/ShaderLibrary.h>

namespace graphics_backend
{
	// Shader reflection data for descriptor bindings
	struct VulkanDescriptorBindingInfo
	{
		uint32_t set;
		uint32_t binding;
		vk::DescriptorType descriptorType;
		uint32_t descriptorCount;
		vk::ShaderStageFlags stageFlags;
		cacore::NameHash name;
	};

	// Push constant range info
	struct VulkanPushConstantInfo
	{
		uint32_t offset;
		uint32_t size;
		vk::ShaderStageFlags stageFlags;
		cacore::NameHash name;
	};

	// Compiled shader module info
	struct VulkanCompiledShaderInfo
	{
		vk::ShaderModule shaderModule;
		vk::ShaderStageFlagBits stage;
		castl::vector<VulkanDescriptorBindingInfo> descriptorBindings;
		castl::vector<VulkanPushConstantInfo> pushConstants;
		cacore::NameHash entryPoint;
		// SPIR-V bytecode (kept for shader module creation)
		castl::vector<uint32_t> spirvCode;
		// reflection data from ShaderCompilerSlang
		ShaderCompilerSlang::ShaderReflectionData reflectionData;
	};

	// Task 3.1: Hierarchy element for IterateHierarchyElements helper
	struct VulkanHierarchyElement
	{
		ShaderCompilerSlang::ShaderBindingHierarchy const* pHierarchy;
		uint32_t hierarchyID;
		uint32_t offset;
	};

	// Task 3.1: IterateHierarchyElements helper function (references D3D12 implementation)
	void IterateHierarchyElements(
		ShaderCompilerSlang::ShaderReflectionData const* reflectionData,
		castl::function<void(VulkanHierarchyElement const&)> hierarchyElementCallback);

	// Task 3.2: Construct VulkanShaderResourceBindingInfo from reflection data
	VulkanShaderResourceBindingInfo ConstructShaderDescriptorInfo(
		const char* pathName,
		ShaderCompilerSlang::ShaderReflectionData const& shaderReflectionData);

	class ShaderImporter_Vulkan : public VulkanSubobjectBase
	{
	public:
		ShaderImporter_Vulkan() = default;
		~ShaderImporter_Vulkan() = default;

		void Init();
		virtual void Release() override;

		// FR-013: Set compiler manager (following D3D12ShaderResourceImporter pattern)
		void SetCompiler(ShaderCompilerSlang::IShaderCompilerManager* compiler);

		// FR-013: Compile shader from source file using IShaderCompilerManager
		bool CompileFromSource(
			castl::string const& sourcePath,
			castl::string const& entryPoint,
			vk::ShaderStageFlagBits stage,
			VulkanCompiledShaderInfo& outInfo);

		// Compile shader from SPIR-V bytecode (for pre-compiled shaders)
		bool CompileFromSPIRV(
			vk::ShaderStageFlagBits stage,
			castl::vector<uint32_t> const& spirvCode,
			cacore::NameHash const& entryPoint,
			VulkanCompiledShaderInfo& outInfo);

		// Get descriptor set layout bindings from compiled shader
		castl::vector<vk::DescriptorSetLayoutBinding> GetDescriptorSetLayoutBindings(
			VulkanCompiledShaderInfo const& shaderInfo,
			uint32_t setIndex) const;

		// Get push constant ranges from compiled shader
		castl::vector<vk::PushConstantRange> GetPushConstantRanges(
			VulkanCompiledShaderInfo const& shaderInfo) const;

	private:
		// Create shader module from SPIR-V
		vk::ShaderModule CreateShaderModule(castl::vector<uint32_t> const& spirvCode);

		// FR-013: Extract descriptor bindings from ShaderCompilerSlang reflection data
		void ExtractBindingsFromReflection(
			ShaderCompilerSlang::ShaderReflectionData const& reflectionData,
			VulkanCompiledShaderInfo& outInfo);

		// Convert ShaderCompilerSlang resource type to Vulkan descriptor type
		vk::DescriptorType ConvertToDescriptorType(
			ShaderCompilerSlang::EShaderResourceType resourceType,
			ShaderCompilerSlang::EShaderResourceAccess access) const;

		// IShaderCompilerManager instance (FR-013)
		ShaderCompilerSlang::IShaderCompilerManager* m_ShaderCompilerManager = nullptr;

		castl::vector<vk::ShaderModule> m_CreatedModules;
	};
}
