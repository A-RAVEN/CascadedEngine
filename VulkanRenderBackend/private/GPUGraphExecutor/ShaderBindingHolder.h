#pragma once
#include <ShaderDescriptorSetAllocator.h>
#include "ShaderArgList.h"
#include <Compiler.h>
#include <CVulkanBufferObject.h>

namespace graphics_backend
{
	class ShadderResourceProvider
	{
	public:
		virtual vk::Buffer GetBufferFromHandle(BufferHandle const& handle) = 0;
		virtual vk::ImageView GetImageView(ImageHandle const& handle) = 0;
	};

	class ShaderBindingInstance
	{
	public:
		void InitShaderBindings(CVulkanApplication& application, ShaderCompilerSlang::ShaderReflectionData const& reflectionData);
		void WriteShaderData(CVulkanApplication& application
			, ShadderResourceProvider& resourceProvider
			, vk::CommandBuffer& command
			, ShaderArgList const& shaderArgList);
		castl::vector<vk::DescriptorSet> m_DescriptorSets;
		castl::vector<vk::DescriptorSetLayout> m_DescriptorSetsLayouts;
		castl::vector<VulkanBufferHandle> m_UniformBuffers;
		ShaderCompilerSlang::ShaderReflectionData const* p_ReflectionData;
		CVulkanApplication* p_Application;
	};
}