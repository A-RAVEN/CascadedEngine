#pragma once
#include "VulkanIncludes.h"
#include "ResourceUsageInfo.h"
#include <map>
#include <deque>
#include <RenderInterface/header/Common.h>
namespace graphics_backend
{


	class VulkanBarrierCollector
	{
	public:
		VulkanBarrierCollector(uint32_t currentQueueFamilyIndex) : m_CurrentQueueFamilyIndex(currentQueueFamilyIndex){}

		void PushImageBarrier(vk::Image image
			, ETextureFormat format
			, ResourceUsageFlags sourceUsage
			, ResourceUsageFlags destUsage);

		void PushBufferBarrier(vk::Buffer buffer
			, ResourceUsageFlags sourceUsage
			, ResourceUsageFlags destUsage);

		void ExecuteBarrier(vk::CommandBuffer commandBuffer);

		void Clear();

		struct BarrierGroup
		{
			std::vector<std::tuple<ResourceUsageVulkanInfo, ResourceUsageVulkanInfo, vk::Image, ETextureFormat>> m_Images;
			std::vector<std::tuple<ResourceUsageVulkanInfo, ResourceUsageVulkanInfo, vk::Buffer>> m_Buffers;
		};

	private:

		void ExecuteCurrentQueueBarriers(vk::CommandBuffer commandBuffer);

		uint32_t m_CurrentQueueFamilyIndex;
		std::map<std::tuple<vk::PipelineStageFlags, vk::PipelineStageFlags>
			, BarrierGroup> m_BarrierGroups;

		std::map<std::tuple<vk::PipelineStageFlags, vk::PipelineStageFlags, uint32_t>
			, BarrierGroup> m_ReleaseGroups;

		std::map<std::tuple<vk::PipelineStageFlags, vk::PipelineStageFlags, uint32_t>
			, BarrierGroup> m_AquireGroups;
	};
}