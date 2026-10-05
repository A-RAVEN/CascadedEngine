#pragma once
#include <CASTL/CAVector.h>
#include <Utils/VulkanIncludes.h>
namespace graphics_backend
{
	struct VertexInputStatesCache
	{
		//VkPipelineVertexInputStateCreateInfo
		castl::vector<vk::VertexInputBindingDescription> vertexInputBindings;
		castl::vector<vk::VertexInputAttributeDescription> vertexInputAttributes;
		//VkPipelineInputAssemblyStateCreateInfo
		vk::PipelineInputAssemblyStateCreateFlags flags;
		vk::PrimitiveTopology topology;
		bool primitiveRestartEnable;

	public:
		void GetPipelineVertexInputStateCreateInfo(vk::PipelineVertexInputStateCreateInfo& outCreateInfo) const
		{
			outCreateInfo.setVertexBindingDescriptions(vertexInputBindings);
			outCreateInfo.setVertexAttributeDescriptions(vertexInputAttributes);
		}
		void GetPipelineInputAssemblyStateCreateInfo(vk::PipelineInputAssemblyStateCreateInfo& outCreateInfo) const
		{
			outCreateInfo.flags = flags;
			outCreateInfo.topology = topology;
			outCreateInfo.primitiveRestartEnable = primitiveRestartEnable;
		}
	};
}

// 顶点输入状态也是管线对象的去重键（`cacore::hash<…, cahash::sha256_hash>`）：Vulkan 句柄与包装结构
// 拼成的形状进不了生成表，按设计给的替代做法手写哈希特化（按声明顺序逐字段，容器走新运行时规则）。
namespace cacore::codegen
{
	template<>
	struct custom_hash_trait<graphics_backend::VertexInputStatesCache>
	{
		static void hash(graphics_backend::VertexInputStatesCache const& obj, auto& hasher)
		{
			hasher.hash(obj.vertexInputBindings);
			hasher.hash(obj.vertexInputAttributes);
			hasher.hash(obj.flags);
			hasher.hash(obj.topology);
			hasher.hash(obj.primitiveRestartEnable);
		}
	};
}