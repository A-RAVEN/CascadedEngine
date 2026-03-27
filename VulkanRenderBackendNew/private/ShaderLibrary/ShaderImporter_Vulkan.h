#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>
#include <CACore/CAHash.h>
#include <vk_mem_alloc.h>
#include <Interface/ShaderCompiler/header/Compiler.h>

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
		// Reflection data from ShaderCompilerSlang
		ShaderCompilerSlang::ShaderReflectionData reflectionData;
	};

	// Vulkan shader resource binding info (similar to D3D12's ShaderResourceBindingInfo)
	struct VulkanShaderResourceBindingInfo
	{
		struct CBufferInfo
		{
			uint32_t set;
			uint32_t binding;
			uint32_t elementCount;
			cacore::NameHash name;
		};

		struct ImageInfo
		{
			uint32_t set;
			uint32_t binding;
			uint32_t elementCount;
			cacore::NameHash name;
			ShaderCompilerSlang::EShaderResourceAccess access;
			ShaderCompilerSlang::EShaderResourceType resourceType;
		};

		struct BufferInfo
		{
			uint32_t set;
			uint32_t binding;
			uint32_t elementCount;
			cacore::NameHash name;
			ShaderCompilerSlang::EShaderResourceAccess access;
			ShaderCompilerSlang::EShaderResourceType resourceType;
		};

		struct SamplerInfo
		{
			uint32_t set;
			uint32_t binding;
			uint32_t elementCount;
			cacore::NameHash name;
		};

		castl::vector<CBufferInfo> cbufferInfos;
		castl::vector<ImageInfo> imageInfos;
		castl::vector<BufferInfo> bufferInfos;
		castl::vector<SamplerInfo> samplerInfos;
	};

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

		// FR-013: Construct Vulkan descriptor bindings from ShaderReflectionData
		VulkanShaderResourceBindingInfo ConstructShaderDescriptorInfo(
			ShaderCompilerSlang::ShaderReflectionData const& reflectionData);

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
