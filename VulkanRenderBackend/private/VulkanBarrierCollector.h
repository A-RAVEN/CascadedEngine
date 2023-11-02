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

		//void PushImageReleaseBarrier(vk::Image image
		//	, ResourceUsageFlags sourceUsage
		//	, ResourceUsageFlags destUsage
		//	, uint32_t destQueueFamily);

		//void PushImageAquireBarrier(vk::Image image
		//	, ResourceUsageFlags sourceUsage
		//	, ResourceUsageFlags destUsage
		//	, uint32_t sourceQueueFamily);

		void ExecuteBarrier(vk::CommandBuffer commandBuffer);


		struct BarrierGroup
		{
			std::vector<std::tuple<ResourceUsageVulkanInfo, ResourceUsageVulkanInfo, vk::Image, ETextureFormat>> m_Images;
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