#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <CASTL/CAVector.h>
#include <PipelineStates/VertexInputStates.h>
#include <PipelineStates/FragmentOutputStates.h>
#include <PipelineStates/ShaderModule.h>

namespace graphics_backend
{
	// Pipeline library parts for VK_EXT_graphics_pipeline_library
	struct PipelineLibraryParts
	{
		vk::Pipeline vertexInputLibrary;
		vk::Pipeline preRasterizationLibrary;
		vk::Pipeline fragmentLibrary;
		vk::Pipeline fragmentOutputLibrary;
		bool isComplete = false;
	};

	// Key for caching pipeline libraries
	struct PipelineLibraryKey
	{
		VKHashVal vertexInputHash;
		VKHashVal fragmentOutputHash;
		VKHashVal shaderHash;

		auto operator<=>(PipelineLibraryKey const& other) const = default;
	};

	class VulkanPipelineLibrary : public VulkanSubobjectBase
	{
	public:
		VulkanPipelineLibrary() = default;
		~VulkanPipelineLibrary() = default;

		void Init();
		virtual void Release() override;

		// Check if pipeline library extension is supported
		bool IsSupported() const { return m_Supported; }

		// Create vertex input interface library
		vk::Pipeline CreateVertexInputLibrary(
			vk::PipelineVertexInputStateCreateInfo const& vertexInputState,
			vk::PipelineInputAssemblyStateCreateInfo const& inputAssemblyState);

		// Create pre-rasterization shaders library
		vk::Pipeline CreatePreRasterizationLibrary(
			vk::PipelineShaderStageCreateInfo const& vertexShader,
			vk::PipelineShaderStageCreateInfo const* tessControlShader,
			vk::PipelineShaderStageCreateInfo const* tessEvalShader,
			vk::PipelineShaderStageCreateInfo const* geometryShader,
			vk::PipelineViewportStateCreateInfo const& viewportState,
			vk::PipelineRasterizationStateCreateInfo const& rasterizationState);

		// Create fragment shader library
		vk::Pipeline CreateFragmentLibrary(
			vk::PipelineShaderStageCreateInfo const& fragmentShader);

		// Create fragment output interface library
		vk::Pipeline CreateFragmentOutputLibrary(
			vk::PipelineMultisampleStateCreateInfo const& multisampleState,
			vk::PipelineDepthStencilStateCreateInfo const* depthStencilState,
			vk::PipelineColorBlendStateCreateInfo const& colorBlendState);

		// Link full pipeline from libraries
		vk::Pipeline LinkPipeline(
			PipelineLibraryParts const& libraries,
			vk::PipelineLayout layout,
			vk::RenderPass renderPass,
			uint32_t subpass);

		// Create monolithic pipeline (fallback when library not supported)
		vk::Pipeline CreateMonolithicPipeline(
			vk::GraphicsPipelineCreateInfo const& createInfo);

		// Get cached library parts
		PipelineLibraryParts const* GetCachedLibrary(PipelineLibraryKey const& key) const;

		// Cache library parts
		void CacheLibrary(PipelineLibraryKey const& key, PipelineLibraryParts const& parts);

	private:
		bool m_Supported = false;

		// Cache for pipeline libraries
		castl::vector<vk::Pipeline> m_CreatedPipelines;

		// Library cache (simplified - would use hash map in production)
		castl::vector<castl::pair<PipelineLibraryKey, PipelineLibraryParts>> m_LibraryCache;
	};
}
