#pragma once
#include "VulkanApplicationSubobjectBase.h"
#include "CShaderModuleObject.h"
#include "RenderPassObject.h"
#include "VulkanPipelineObject.h"
#include "FramebufferObject.h"
#include "ShaderDescriptorSetAllocator.h"
#include "TextureSampler_Impl.h"
#include <DescriptorAllocation/DescriptorLayoutPool.h>
#include <GPUObject/ComputePipelineObject.h>

namespace graphics_backend
{
	class GPUObjectManager : public VKAppSubObjectBaseNoCopy
	{
	public:
		GPUObjectManager(CVulkanApplication& application);
		RenderPassObjectDic& GetRenderPassCache() { return m_RenderPassCache; }
		FramebufferObjectDic& GetFramebufferCache() { return m_FramebufferObjectCache; }
		PipelineObjectDic& GetPipelineCache() { return m_PipelineObjectCache; }
		TextureSamplerObjectDic& GetTextureSamplerCache() { return m_TextureSamplerCache; }
		ComputePipelineObjectDic& GetComputePipelineCache() { return m_ComputePipelineCache; }
		ShaderModuleObjectDic m_ShaderModuleCache;
		DescriptorSetAllocatorDic m_DescriptorSetAllocatorDic;
	private:
		RenderPassObjectDic m_RenderPassCache;
		FramebufferObjectDic m_FramebufferObjectCache;
		PipelineObjectDic m_PipelineObjectCache;
		TextureSamplerObjectDic m_TextureSamplerCache;
		ComputePipelineObjectDic m_ComputePipelineCache;
	};
}