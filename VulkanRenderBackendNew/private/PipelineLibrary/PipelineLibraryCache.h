#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>
#include <Hasher.h>

namespace graphics_backend
{
	// Render state combination for pipeline caching
	struct RenderStateCombination
	{
		// Vertex input state
		castl::vector<vk::VertexInputBindingDescription> vertexBindings;
		castl::vector<vk::VertexInputAttributeDescription> vertexAttributes;
		vk::PrimitiveTopology topology;

		// Fragment output state
		castl::vector<vk::PipelineColorBlendAttachmentState> blendAttachments;
		vk::Format depthFormat;
		castl::vector<vk::Format> colorFormats;
		vk::SampleCountFlagBits sampleCount;

		// Depth/stencil state
		bool depthTestEnable = false;
		bool depthWriteEnable = false;
		bool stencilTestEnable = false;
		vk::CompareOp depthCompareOp = vk::CompareOp::eAlways;
		vk::StencilOpState stencilFront{};
		vk::StencilOpState stencilBack{};

		// Input assembly
		bool primitiveRestartEnable = false;

		// Rasterization state. These drive vk::PipelineRasterizationStateCreateInfo behavior (and thus
		// the resulting VkPipeline): polygon mode, face culling, front-face winding, and depth clamp.
		// lineWidth is NOT included - the backend hardcodes lineWidth = 1.0f (VulkanGraphExecutor.cpp:1839)
		// and never reads a per-pass value, so including it would add a dimension that never varies.
		// discardRasterization / depth-bias are not yet mapped to Vulkan either (same rationale).
		vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
		vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone;
		vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
		bool depthClampEnable = false;

		// Shader hashes
		VKHashVal vertexShaderHash;
		VKHashVal fragmentShaderHash;

		auto operator<=>(RenderStateCombination const& other) const = default;
	};

	class PipelineLibraryCache : public VulkanSubobjectBase
	{
	public:
		PipelineLibraryCache() = default;
		~PipelineLibraryCache() = default;

		void Init();
		virtual void Release() override;

		// Generate hash key from render state combination
		size_t GenerateHashKey(RenderStateCombination const& state) const;

		// Try to get cached pipeline
		vk::Pipeline TryGetCachedPipeline(size_t hashKey) const;

		// Cache a pipeline
		void CachePipeline(size_t hashKey, vk::Pipeline pipeline);

		// Get cache statistics
		size_t GetCacheSize() const { return m_PipelineCache.size(); }
		size_t GetCacheHits() const { return m_CacheHits; }
		size_t GetCacheMisses() const { return m_CacheMisses; }

		// Clear cache
		void ClearCache();

	private:
		castl::unordered_map<size_t, vk::Pipeline> m_PipelineCache;
		mutable size_t m_CacheHits = 0;
		mutable size_t m_CacheMisses = 0;
	};
}
