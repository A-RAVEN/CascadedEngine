#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAUnorderedMap.h>
#include <cstring>
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

	// Hash functor for PipelineLibraryKey:
	//   XOR of the first sizeof(size_t) bytes from each of the three 32-byte SHA-256 hashes.
	//   On 64-bit builds this produces a 64-bit hash (birthday bound ~2^32 entries);
	//   on 32-bit builds only 32 bits (birthday bound ~2^16). Practical pipeline library
	//   cache sizes are typically < 100 entries, making collision probability negligible
	//   in practice on either platform.
	//   PipelineLibraryKeyEqual provides full three-field comparison as a safety net —
	//   even in the astronomically unlikely event of a hash collision, the unordered_map
	//   falls back to complete key comparison, ensuring NO incorrect pipeline is ever returned.
	struct PipelineLibraryKeyHash
	{
		size_t operator()(PipelineLibraryKey const& k) const
		{
			// memcpy (elided to a register load on x86-64) avoids strict-aliasing UB
			// from reinterpret_cast on VKHashVal (unsigned char[32], alignof=1) read
			// through a size_t lvalue (alignof=8).
			auto extractFirst = [](VKHashVal const& h) -> size_t
			{
				size_t val;
				memcpy(&val, &h, sizeof(size_t));
				return val;
			};
			return extractFirst(k.vertexInputHash)
				^ extractFirst(k.fragmentOutputHash)
				^ extractFirst(k.shaderHash);
		}
	};

	struct PipelineLibraryKeyEqual
	{
		bool operator()(PipelineLibraryKey const& a, PipelineLibraryKey const& b) const
		{
			return a == b;
		}
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
			vk::PipelineInputAssemblyStateCreateInfo const& inputAssemblyState,
			vk::PipelineCache cache = nullptr);

		// Create pre-rasterization shaders library
		vk::Pipeline CreatePreRasterizationLibrary(
			vk::PipelineShaderStageCreateInfo const& vertexShader,
			vk::PipelineShaderStageCreateInfo const* tessControlShader,
			vk::PipelineShaderStageCreateInfo const* tessEvalShader,
			vk::PipelineShaderStageCreateInfo const* geometryShader,
			vk::PipelineLayout layout,
			vk::PipelineViewportStateCreateInfo const& viewportState,
			vk::PipelineRasterizationStateCreateInfo const& rasterizationState,
			vk::PipelineDynamicStateCreateInfo const* pDynamicState = nullptr,
			vk::PipelineCache cache = nullptr);

		// Create fragment shader library
		vk::Pipeline CreateFragmentLibrary(
			vk::PipelineShaderStageCreateInfo const& fragmentShader,
			vk::PipelineLayout layout,
			vk::RenderPass renderPass,
			vk::PipelineDepthStencilStateCreateInfo const* pDepthStencilState = nullptr,
			vk::PipelineCache cache = nullptr);

		// Create fragment output interface library
		vk::Pipeline CreateFragmentOutputLibrary(
			vk::PipelineMultisampleStateCreateInfo const& multisampleState,
			vk::PipelineDepthStencilStateCreateInfo const* depthStencilState,
			vk::PipelineColorBlendStateCreateInfo const& colorBlendState,
			vk::RenderPass renderPass,
			vk::PipelineCache cache = nullptr);

		// Link full pipeline from libraries
		vk::Pipeline LinkPipeline(
			PipelineLibraryParts const& libraries,
			vk::PipelineLayout layout,
			vk::RenderPass renderPass,
			uint32_t subpass,
			vk::PipelineCache cache = nullptr);

		// Create monolithic pipeline (fallback when library not supported)
		vk::Pipeline CreateMonolithicPipeline(
			vk::GraphicsPipelineCreateInfo const& createInfo,
			vk::PipelineCache cache = nullptr);

		// Get cached library parts
		PipelineLibraryParts const* GetCachedLibrary(PipelineLibraryKey const& key) const;

		// Cache library parts
		void CacheLibrary(PipelineLibraryKey const& key, PipelineLibraryParts const& parts);

	private:
		bool m_Supported = false;

		// Cache for pipeline libraries
		castl::vector<vk::Pipeline> m_CreatedPipelines;

		// Library cache — hash map with collision-safe key comparison
		castl::unordered_map<PipelineLibraryKey, PipelineLibraryParts, PipelineLibraryKeyHash, PipelineLibraryKeyEqual> m_LibraryCache;
	};
}
