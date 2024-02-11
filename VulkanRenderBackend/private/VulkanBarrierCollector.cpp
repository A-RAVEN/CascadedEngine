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

		auto key = castl::make_tuple(sourceInfo.m_UsageStageMask, destInfo.m_UsageStageMask);
		auto found = m_BarrierGroups.find(key);
		if (found == m_BarrierGroups.end())
		{
			m_BarrierGroups.emplace(key, BarrierGroup{});
			found = m_BarrierGroups.find(key);
		}

		found->second.m_Images.push_back(castl::make_tuple(sourceInfo, destInfo, image, format));
	}

	void VulkanBarrierCollector::PushBufferBarrier(vk::Buffer buffer, ResourceUsageFlags sourceUsage, ResourceUsageFlags destUsage)
	{
		ResourceUsageVulkanInfo sourceInfo = GetUsageInfo(sourceUsage);
		ResourceUsageVulkanInfo destInfo = GetUsageInfo(destUsage);

		auto key = castl::make_tuple(sourceInfo.m_UsageStageMask, destInfo.m_UsageStageMask);
		auto found = m_BarrierGroups.find(key);
		if (found == m_BarrierGroups.end())
		{
			m_BarrierGroups.emplace(key, BarrierGroup{});
			found = m_BarrierGroups.find(key);
		}

		found->second.m_Buffers.push_back(castl::make_tuple(sourceInfo, destInfo, buffer));
	}
	
	void VulkanBarrierCollector::ExecuteBarrier(vk::CommandBuffer commandBuffer)
	{
		ExecuteCurrentQueueBarriers(commandBuffer);
	}

	void VulkanBarrierCollector::Clear()
	{
		m_BarrierGroups.clear();
		m_ReleaseGroups.clear();
		m_AquireGroups.clear();
	}
	
	void VulkanBarrierCollector::ExecuteCurrentQueueBarriers(vk::CommandBuffer commandBuffer)
	{
		castl::vector<vk::ImageMemoryBarrier> imageBarriers;
		castl::vector<vk::BufferMemoryBarrier> bufferBarriers;
		for (auto& key_value : m_BarrierGroups)
		{
			auto key = key_value.first;
			imageBarriers.clear();
			bufferBarriers.clear();
			imageBarriers.reserve(key_value.second.m_Images.size());
			bufferBarriers.reserve(key_value.second.m_Buffers.size());
			for (auto& imgInfo : key_value.second.m_Images)
			{
				ResourceUsageVulkanInfo& sourceInfo = castl::get<0>(imgInfo);
				ResourceUsageVulkanInfo& destInfo = castl::get<1>(imgInfo);
				vk::Image image = castl::get<2>(imgInfo);
				ETextureFormat format = castl::get<3>(imgInfo);

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

			for (auto& bufferInfo : key_value.second.m_Buffers)
			{
				ResourceUsageVulkanInfo& sourceInfo = castl::get<0>(bufferInfo);
				ResourceUsageVulkanInfo& destInfo = castl::get<1>(bufferInfo);
				vk::Buffer buffer = castl::get<2>(bufferInfo);

				vk::BufferMemoryBarrier newBarrier(
					sourceInfo.m_UsageAccessFlags
					, destInfo.m_UsageAccessFlags
					, m_CurrentQueueFamilyIndex
					, m_CurrentQueueFamilyIndex
					, buffer
					, 0
					, VK_WHOLE_SIZE);
				bufferBarriers.push_back(newBarrier);
			}

			commandBuffer.pipelineBarrier(
				castl::get<0>(key)
				, castl::get<1>(key)
				, {}
				, {}
				, bufferBarriers
				, imageBarriers);
		}

	}
}