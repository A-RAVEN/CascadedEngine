#pragma once
#include <Common.h>
#include <CASTL/CAMap.h>
#include <CASTL/CADeque.h>
#include "VulkanIncludes.h"
#include "ResourceUsageInfo.h"

namespace graphics_backend
{
	class VulkanBarrierCollector
	{
	public:
		VulkanBarrierCollector() = default;
		VulkanBarrierCollector(uint32_t currentQueueFamilyIndex) : m_CurrentQueueFamilyIndex(currentQueueFamilyIndex){}

		void SetCurrentQueueFamilyIndex(uint32_t currentQueueFamilyIndex) { m_CurrentQueueFamilyIndex = currentQueueFamilyIndex; }

		uint32_t GetQueueFamily() const { return m_CurrentQueueFamilyIndex; }

		void PushImageBarrier(vk::Image image
			, ETextureFormat format
			, ResourceUsageFlags sourceUsage
			, ResourceUsageFlags destUsage);

		void PushBufferBarrier(vk::Buffer buffer
			, ResourceUsageFlags sourceUsage
			, ResourceUsageFlags destUsage);

		void PushImageReleaseBarrier(uint32_t targetQueueFamilyIndex
			, vk::Image image
			, ETextureFormat format
			, ResourceUsageFlags sourceUsage
			, ResourceUsageFlags destUsage);

		void PushBufferReleaseBarrier(uint32_t targetQueueFamilyIndex
			, vk::Buffer buffer
			, ResourceUsageFlags sourceUsage
			, ResourceUsageFlags destUsage);

		void PushImageAquireBarrier(uint32_t sourceQueueFamilyIndex
			, vk::Image image
			, ETextureFormat format
			, ResourceUsageFlags sourceUsage
			, ResourceUsageFlags destUsage);

		void PushBufferAquireBarrier(uint32_t sourceQueueFamilyIndex
			, vk::Buffer buffer
			, ResourceUsageFlags sourceUsage
			, ResourceUsageFlags destUsage);

		void ExecuteBarrier(vk::CommandBuffer commandBuffer);

		void ExecuteReleaseBarrier(vk::CommandBuffer commandBuffer);

		void Clear();

		struct BarrierGroup
		{
			castl::vector<castl::tuple<ResourceUsageVulkanInfo, ResourceUsageVulkanInfo, vk::Image, ETextureFormat, uint32_t>> m_Images;
			castl::vector<castl::tuple<ResourceUsageVulkanInfo, ResourceUsageVulkanInfo, vk::Buffer, uint32_t>> m_Buffers;
		};

	private:

		void ExecuteCurrentQueueBarriers(vk::CommandBuffer commandBuffer);

		uint32_t m_CurrentQueueFamilyIndex = 0;
		castl::map<castl::tuple<vk::PipelineStageFlags, vk::PipelineStageFlags>
			, BarrierGroup> m_BarrierGroups;

		castl::map<castl::tuple<vk::PipelineStageFlags, vk::PipelineStageFlags>
			, BarrierGroup> m_ReleaseBarrierGroups;
	};
}