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