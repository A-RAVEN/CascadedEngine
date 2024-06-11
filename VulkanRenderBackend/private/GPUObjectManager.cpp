#include "pch.h"
#include "GPUObjectManager.h"

namespace graphics_backend
{
	GPUObjectManager::GPUObjectManager(CVulkanApplication& app)
		: VKAppSubObjectBaseNoCopy(app)
		, m_RenderPassCache(app)
		, m_FramebufferObjectCache(app)
		, m_PipelineObjectCache(app)
		, m_TextureSamplerCache(app)
		, m_ShaderModuleCache(app)
		, m_DescriptorSetAllocatorDic(app)
		, m_ComputePipelineCache(app)
	{
	}
}

