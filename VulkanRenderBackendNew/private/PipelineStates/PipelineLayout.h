#pragma once
#include <Utils/VulkanIncludes.h>
#include <CASTL/CAVector.h>
namespace graphics_backend
{
	//Vulkan Object
	struct DescriptorSetLayoutCache
	{
		struct DescriptorSetLayoutBindingCache {
			uint32_t              binding;
			vk::DescriptorType      descriptorType;
			uint32_t              descriptorCount;
			vk::ShaderStageFlags    stageFlags;
		};
		castl::vector<DescriptorSetLayoutBindingCache> bindings;
		vk::DescriptorSetLayoutCreateFlags       flags;
	};

	//Vulkan Object
	struct PipelineLayoutCache
	{
		vk::PipelineLayoutCreateFlags flags;
		castl::vector<TypedVKHashVal<DescriptorSetLayoutCache>> descriptorSetLayouts;
		castl::vector<vk::PushConstantRange> pushConstantRanges;
	};
}