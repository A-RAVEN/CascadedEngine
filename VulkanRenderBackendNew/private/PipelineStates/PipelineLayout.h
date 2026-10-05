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

// 这两个 Cache 是管线对象的去重键（`cacore::hash<…, cahash::sha256_hash>`），它们由 Vulkan 的句柄与
// 包装结构拼成、进不了生成表（这个头把整套 Vulkan 头拖进来），所以按设计给的替代做法手写哈希特化：
// 按声明顺序逐字段（容器走新运行时的容器规则，元素用自己的特化）。
namespace cacore::codegen
{
	template<>
	struct custom_hash_trait<graphics_backend::DescriptorSetLayoutCache::DescriptorSetLayoutBindingCache>
	{
		static void hash(graphics_backend::DescriptorSetLayoutCache::DescriptorSetLayoutBindingCache const& obj,
		                 auto& hasher)
		{
			hasher.hash(obj.binding);
			hasher.hash(obj.descriptorType);
			hasher.hash(obj.descriptorCount);
			hasher.hash(obj.stageFlags);
		}
	};

	template<>
	struct custom_hash_trait<graphics_backend::DescriptorSetLayoutCache>
	{
		static void hash(graphics_backend::DescriptorSetLayoutCache const& obj, auto& hasher)
		{
			hasher.hash(obj.bindings);
			hasher.hash(obj.flags);
		}
	};

	template<>
	struct custom_hash_trait<graphics_backend::PipelineLayoutCache>
	{
		static void hash(graphics_backend::PipelineLayoutCache const& obj, auto& hasher)
		{
			hasher.hash(obj.flags);
			hasher.hash(obj.descriptorSetLayouts);
			hasher.hash(obj.pushConstantRanges);
		}
	};
}