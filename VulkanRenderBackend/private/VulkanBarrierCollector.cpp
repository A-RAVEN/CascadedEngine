#include "pch.h"
#include "VulkanBarrierCollector.h"

namespace graphics_backend
{
	void VulkanBarrierCollector::PushImageBarrier(vk::Image image
		, ETextureFormat format
		, ResourceUsageFlags sourceUsage
		, ResourceUsageFlags destUsage)
	{
		PushImageAquireBarrier(m_CurrentQueueFamilyIndex, image, format, sourceUsage, destUsage);
	}

	void VulkanBarrierCollector::PushBufferBarrier(vk::Buffer buffer, ResourceUsageFlags sourceUsage, ResourceUsageFlags destUsage)
	{
		PushBufferAquireBarrier(m_CurrentQueueFamilyIndex, buffer, sourceUsage, destUsage);
	}

	void VulkanBarrierCollector::PushImageReleaseBarrier(uint32_t targetQueueFamilyIndex, vk::Image image, ETextureFormat format, ResourceUsageFlags sourceUsage, ResourceUsageFlags destUsage)
	{
		if (targetQueueFamilyIndex == m_CurrentQueueFamilyIndex)
		{
			return;
		}
		ResourceUsageVulkanInfo sourceInfo = GetUsageInfo(sourceUsage);
		ResourceUsageVulkanInfo destInfo = GetUsageInfo(destUsage);

		auto key = castl::make_tuple(sourceInfo.m_UsageStageMask, destInfo.m_UsageStageMask);
		auto found = m_ReleaseBarrierGroups.find(key);
		if (found == m_ReleaseBarrierGroups.end())
		{
			found = m_ReleaseBarrierGroups.emplace(key, BarrierGroup{}).first;
		}

		found->second.m_Images.push_back(castl::make_tuple(sourceInfo, destInfo, image, format, targetQueueFamilyIndex));
	}

	void VulkanBarrierCollector::PushBufferReleaseBarrier(uint32_t targetQueueFamilyIndex, vk::Buffer buffer, ResourceUsageFlags sourceUsage, ResourceUsageFlags destUsage)
	{
		if (targetQueueFamilyIndex == m_CurrentQueueFamilyIndex)
		{
			return;
		}

		ResourceUsageVulkanInfo sourceInfo = GetUsageInfo(sourceUsage);
		ResourceUsageVulkanInfo destInfo = GetUsageInfo(destUsage);

		auto key = castl::make_tuple(sourceInfo.m_UsageStageMask, destInfo.m_UsageStageMask);
		auto found = m_ReleaseBarrierGroups.find(key);
		if (found == m_ReleaseBarrierGroups.end())
		{
			found = m_ReleaseBarrierGroups.emplace(key, BarrierGroup{}).first;
		}

		found->second.m_Buffers.push_back(castl::make_tuple(sourceInfo, destInfo, buffer, targetQueueFamilyIndex));
	}

	void VulkanBarrierCollector::PushImageAquireBarrier(uint32_t sourceQueueFamilyIndex, vk::Image image, ETextureFormat format, ResourceUsageFlags sourceUsage, ResourceUsageFlags destUsage)
	{
		ResourceUsageVulkanInfo sourceInfo = GetUsageInfo(sourceUsage);
		ResourceUsageVulkanInfo destInfo = GetUsageInfo(destUsage);

		auto key = castl::make_tuple(sourceInfo.m_UsageStageMask, destInfo.m_UsageStageMask);
		auto found = m_BarrierGroups.find(key);
		if (found == m_BarrierGroups.end())
		{
			found = m_BarrierGroups.emplace(key, BarrierGroup{}).first;
		}

		found->second.m_Images.push_back(castl::make_tuple(sourceInfo, destInfo, image, format, sourceQueueFamilyIndex));
	}

	void VulkanBarrierCollector::PushBufferAquireBarrier(uint32_t sourceQueueFamilyIndex, vk::Buffer buffer, ResourceUsageFlags sourceUsage, ResourceUsageFlags destUsage)
	{
		ResourceUsageVulkanInfo sourceInfo = GetUsageInfo(sourceUsage);
		ResourceUsageVulkanInfo destInfo = GetUsageInfo(destUsage);

		auto key = castl::make_tuple(sourceInfo.m_UsageStageMask, destInfo.m_UsageStageMask);
		auto found = m_BarrierGroups.find(key);
		if (found == m_BarrierGroups.end())
		{
			found = m_BarrierGroups.emplace(key, BarrierGroup{}).first;
		}
		found->second.m_Buffers.push_back(castl::make_tuple(sourceInfo, destInfo, buffer, sourceQueueFamilyIndex));
	}
	
	void VulkanBarrierCollector::ExecuteBarrier(vk::CommandBuffer commandBuffer)
	{
		ExecuteCurrentQueueBarriers(commandBuffer);
	}

	void VulkanBarrierCollector::ExecuteReleaseBarrier(vk::CommandBuffer commandBuffer)
	{
		castl::vector<vk::ImageMemoryBarrier> imageBarriers;
		castl::vector<vk::BufferMemoryBarrier> bufferBarriers;
		for (auto& key_value : m_ReleaseBarrierGroups)
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
				uint32_t targetQueueFamilyIndex = castl::get<4>(imgInfo);

				vk::ImageMemoryBarrier newBarrier(
					sourceInfo.m_UsageAccessFlags
					, destInfo.m_UsageAccessFlags
					, sourceInfo.m_UsageImageLayout
					, destInfo.m_UsageImageLayout
					, m_CurrentQueueFamilyIndex
					, targetQueueFamilyIndex
					, image
					, vulkan_backend::utils::MakeSubresourceRange(format));
				imageBarriers.push_back(newBarrier);
			}

			for (auto& bufferInfo : key_value.second.m_Buffers)
			{
				ResourceUsageVulkanInfo& sourceInfo = castl::get<0>(bufferInfo);
				ResourceUsageVulkanInfo& destInfo = castl::get<1>(bufferInfo);
				vk::Buffer buffer = castl::get<2>(bufferInfo);
				uint32_t targetQueueFamilyIndex = castl::get<3>(bufferInfo);

				vk::BufferMemoryBarrier newBarrier(
					sourceInfo.m_UsageAccessFlags
					, destInfo.m_UsageAccessFlags
					, m_CurrentQueueFamilyIndex
					, targetQueueFamilyIndex
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

	void VulkanBarrierCollector::Clear()
	{
		m_BarrierGroups.clear();
		m_ReleaseBarrierGroups.clear();
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
				uint32_t sourceQueueFamily = castl::get<4>(imgInfo);

				vk::ImageMemoryBarrier newBarrier(
					sourceInfo.m_UsageAccessFlags
					, destInfo.m_UsageAccessFlags
					, sourceInfo.m_UsageImageLayout
					, destInfo.m_UsageImageLayout
					, sourceQueueFamily
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
				uint32_t sourceQueueFamily = castl::get<3>(bufferInfo);

				vk::BufferMemoryBarrier newBarrier(
					sourceInfo.m_UsageAccessFlags
					, destInfo.m_UsageAccessFlags
					, sourceQueueFamily
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