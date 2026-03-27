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
		vk::Format colorFormat;
		vk::SampleCountFlagBits sampleCount;

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
