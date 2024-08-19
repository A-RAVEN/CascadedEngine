#include "pch.h"
#include "GPUObjectManager.h"

namespace graphics_backend
{
	GPUObjectManager::GPUObjectManager(CVulkanApplication& app)
		: VKAppSubObjectBaseNoCopy(app)
		, m_RenderPassCache(app)
		, m_PipelineObjectCache(app)
		, m_TextureSamplerCache(app)
		, m_ShaderModuleCache(app)
		, m_DescriptorSetLayoutCache(app)
		, m_ComputePipelineCache(app)
	{
	}
	void GPUObjectManager::Release()
	{
		m_TextureSamplerCache.ReleaseAll();
		m_ShaderModuleCache.ReleaseAll();
		m_DescriptorSetLayoutCache.ReleaseAll();
		m_PipelineObjectCache.ReleaseAll();
		m_ComputePipelineCache.ReleaseAll();
		m_RenderPassCache.ReleaseAll();
	}
}

