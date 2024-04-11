#pragma once
#include "VulkanApplicationSubobjectBase.h"
#include "CShaderModuleObject.h"
#include "RenderPassObject.h"
#include "VulkanPipelineObject.h"
#include "FramebufferObject.h"
#include "ShaderDescriptorSetAllocator.h"
#include "TextureSampler_Impl.h"
#include <DescriptorAllocation/DescriptorLayoutPool.h>

namespace graphics_backend
{
	class GPUObjectManager : public VKAppSubObjectBaseNoCopy
	{
	public:
		GPUObjectManager(CVulkanApplication& application);
		ShaderModuleObjectDic& GetShaderModuleCache() { return m_ShaderModuleCacheOld; }
		RenderPassObjectDic& GetRenderPassCache() { return m_RenderPassCache; }
		FramebufferObjectDic& GetFramebufferCache() { return m_FramebufferObjectCache; }
		PipelineObjectDic& GetPipelineCache() { return m_PipelineObjectCache; }
		ShaderDescriptorSetAllocatorPool& GetShaderDescriptorPoolCache() { return m_ShaderDescriptorPoolCache; }
		TextureSamplerObjectDic& GetTextureSamplerCache() { return m_TextureSamplerCache; }
		ShaderModuleObjectDic1 m_ShaderModuleCache;
		DescriptorPoolDic m_DescriptorSetPoolDic;
		DescriptorSetAllocatorDic m_DescriptorSetAllocatorDic;
		//Release framebound resources
		void ReleaseFrameboundResources(FrameType releasingFrame);
	private:
		ShaderModuleObjectDic m_ShaderModuleCacheOld;
		RenderPassObjectDic m_RenderPassCache;
		FramebufferObjectDic m_FramebufferObjectCache;
		PipelineObjectDic m_PipelineObjectCache;
		ShaderDescriptorSetAllocatorPool m_ShaderDescriptorPoolCache;
		TextureSamplerObjectDic m_TextureSamplerCache;
	};
}