#include "pch.h"
#include "GPUObjectManager.h"

namespace graphics_backend
{
	GPUObjectManager::GPUObjectManager(CVulkanApplication& application)
		: VKAppSubObjectBaseNoCopy(application)
		//, m_ShaderModuleCacheOld(application)
		, m_RenderPassCache(application)
		, m_FramebufferObjectCache(application)
		, m_PipelineObjectCache(application)
		//, m_ShaderDescriptorPoolCache(application)
		, m_TextureSamplerCache(application)
		, m_ShaderModuleCache(application)
		//, m_DescriptorSetPoolDic(application)
		, m_DescriptorSetAllocatorDic(application)
	{
	}
	//void GPUObjectManager::ReleaseFrameboundResources(FrameType releasingFrame)
	//{
	//	//m_ShaderDescriptorPoolCache.Foreach([releasingFrame](ShaderDescriptorSetLayoutInfo const& info
	//	//	, ShaderDescriptorSetAllocator* pool)
	//	//	{
	//	//		pool->ReleaseFrameboundResources();
	//	//	});
	//}
}

