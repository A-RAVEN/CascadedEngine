#pragma once
#include <ShaderDescriptorSetAllocator.h>
#include "ShaderArgList.h"
#include <Compiler.h>
#include <ResourcePool/FrameBoundResourcePool.h>
#include <GPUResources/GPUResourceInternal.h>

namespace graphics_backend
{
	class ShadderResourceProvider
	{
	public:
		virtual vk::Buffer GetBufferFromHandle(BufferHandle const& handle) = 0;
		virtual vk::ImageView GetImageView(ImageHandle const& handle, GPUTextureView const& view) = 0;
	};

	class ShaderBindingInstance
	{
	public:
		void InitShaderBindings(CVulkanApplication& application, FrameBoundResourcePool* pResourcePool, ShaderCompilerSlang::ShaderReflectionData const& reflectionData);
		void WriteShaderData(CVulkanApplication& application
			, ShadderResourceProvider& resourceProvider
			, FrameBoundResourcePool* pResourcePool
			, vk::CommandBuffer& command
			, ShaderArgList const& shaderArgList);
		castl::vector<vk::DescriptorSet> m_DescriptorSets;
		castl::vector<vk::DescriptorSetLayout> m_DescriptorSetsLayouts;
		castl::vector<VKBufferObject> m_UniformBuffers;
		ShaderCompilerSlang::ShaderReflectionData const* p_ReflectionData;
		CVulkanApplication* p_Application;
	};
}