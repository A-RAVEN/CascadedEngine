#include <PipelineLibrary/PipelineLibraryCache.h>
#include <RenderBackend_Vulkan.h>
#include <CACore/CAHash.h>

namespace graphics_backend
{
	void PipelineLibraryCache::Init()
	{
		CA_LOG_INFO("PipelineLibraryCache initialized");
	}

	void PipelineLibraryCache::Release()
	{
		ClearCache();
		CA_LOG_INFO("PipelineLibraryCache released - final stats: hits={}, misses={}"
			, m_CacheHits, m_CacheMisses);
	}

	size_t PipelineLibraryCache::GenerateHashKey(RenderStateCombination const& state) const
	{
		// Combine all state into a single hash
		size_t hash = 0;

		// Hash vertex bindings
		for (auto const& binding : state.vertexBindings)
		{
			hash = cacore::hash_combine(hash, binding.binding);
			hash = cacore::hash_combine(hash, binding.stride);
			hash = cacore::hash_combine(hash, static_cast<uint32_t>(binding.inputRate));
		}

		// Hash vertex attributes
		for (auto const& attr : state.vertexAttributes)
		{
			hash = cacore::hash_combine(hash, attr.location);
			hash = cacore::hash_combine(hash, attr.binding);
			hash = cacore::hash_combine(hash, attr.format);
			hash = cacore::hash_combine(hash, attr.offset);
		}

		// Hash topology
		hash = cacore::hash_combine(hash, static_cast<uint32_t>(state.topology));

		// Hash blend attachments
		for (auto const& blend : state.blendAttachments)
		{
			hash = cacore::hash_combine(hash, blend.colorWriteMask);
			hash = cacore::hash_combine(hash, blend.blendEnable ? 1u : 0u);
		}

		// Hash formats
		hash = cacore::hash_combine(hash, static_cast<uint32_t>(state.depthFormat));
		hash = cacore::hash_combine(hash, static_cast<uint32_t>(state.colorFormat));
		hash = cacore::hash_combine(hash, static_cast<uint32_t>(state.sampleCount));

		// Hash shader hashes
		for (uint32_t i = 0; i < 8; ++i)
		{
			hash = cacore::hash_combine(hash, state.vertexShaderHash.hashVal.data[i]);
			hash = cacore::hash_combine(hash, state.fragmentShaderHash.hashVal.data[i]);
		}

		return hash;
	}

	vk::Pipeline PipelineLibraryCache::TryGetCachedPipeline(size_t hashKey) const
	{
		auto it = m_PipelineCache.find(hashKey);
		if (it != m_PipelineCache.end())
		{
			++m_CacheHits;
			CA_LOG_INFO("PipelineLibraryCache: Cache hit for key {}", hashKey);
			return it->second;
		}

		++m_CacheMisses;
		return nullptr;
	}

	void PipelineLibraryCache::CachePipeline(size_t hashKey, vk::Pipeline pipeline)
	{
		if (!pipeline)
		{
			CA_LOG_WARN("PipelineLibraryCache: Attempting to cache null pipeline");
			return;
		}

		m_PipelineCache[hashKey] = pipeline;
		CA_LOG_INFO("PipelineLibraryCache: Cached pipeline for key {} (total: {})"
			, hashKey, m_PipelineCache.size());
	}

	void PipelineLibraryCache::ClearCache()
	{
		auto device = GetDevice();
		for (auto& [key, pipeline] : m_PipelineCache)
		{
			if (pipeline)
			{
				device.destroyPipeline(pipeline);
			}
		}
		m_PipelineCache.clear();
		CA_LOG_INFO("PipelineLibraryCache: Cache cleared");
	}
}
