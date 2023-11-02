#include "pch.h"
#include "VulkanBarrierCollector.h"

namespace graphics_backend
{
	void VulkanBarrierCollector::PushImageBarrier(vk::Image image
		, ETextureFormat format
		, ResourceUsageFlags sourceUsage
		, ResourceUsageFlags destUsage)
	{
		ResourceUsageVulkanInfo sourceInfo = GetUsageInfo(sourceUsage);
		ResourceUsageVulkanInfo destInfo = GetUsageInfo(destUsage);

		auto key = std::make_tuple(sourceInfo.m_UsageStageMask, destInfo.m_UsageStageMask);
		auto found = m_BarrierGroups.find(key);
		if (found == m_BarrierGroups.end())
		{
			m_BarrierGroups.emplace(key, BarrierGroup{});
			found = m_BarrierGroups.find(key);
		}

		found->second.m_Images.push_back(std::make_tuple(sourceInfo, destInfo, image, format));
	}
	
	void VulkanBarrierCollector::ExecuteBarrier(vk::CommandBuffer commandBuffer)
	{
		ExecuteCurrentQueueBarriers(commandBuffer);
	}

	
	void VulkanBarrierCollector::ExecuteCurrentQueueBarriers(vk::CommandBuffer commandBuffer)
	{
		for (auto& key_value : m_BarrierGroups)
		{
			std::vector<vk::ImageMemoryBarrier> imageBarriers;
			auto key = key_value.first;
			imageBarriers.reserve(key_value.second.m_Images.size());
			for (auto& imgInfo : key_value.second.m_Images)
			{
				ResourceUsageVulkanInfo& sourceInfo = std::get<0>(imgInfo);
				ResourceUsageVulkanInfo& destInfo = std::get<1>(imgInfo);
				vk::Image image = std::get<2>(imgInfo);
				ETextureFormat format = std::get<3>(imgInfo);

				vk::ImageMemoryBarrier newBarrier(
					sourceInfo.m_UsageAccessFlags
					, destInfo.m_UsageAccessFlags
					, sourceInfo.m_UsageImageLayout
					, destInfo.m_UsageImageLayout
					, m_CurrentQueueFamilyIndex
					, m_CurrentQueueFamilyIndex
					, image
					, vulkan_backend::utils::MakeSubresourceRange(format));
				imageBarriers.push_back(newBarrier);
			}
			commandBuffer.pipelineBarrier(
				std::get<0>(key)
				, std::get<1>(key)
				, {}
				, {}
				, {}
				, imageBarriers);
		}

	}
}