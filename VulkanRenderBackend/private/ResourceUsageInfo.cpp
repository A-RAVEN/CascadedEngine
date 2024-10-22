#include "pch.h"
#include "ResourceUsageInfo.h"

namespace graphics_backend
{
	const ResourceUsageVulkanInfo s_NoneUsageVulkanInfo{
		vk::PipelineStageFlagBits::eAllCommands,
		vk::AccessFlagBits::eNone,
		vk::ImageLayout::eUndefined,
	};

	const static ResourceUsageVulkanInfo s_ResourceUsageToVulkanInfo[] = {
		//eTransferSource
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlagBits::eTransferRead,
			vk::ImageLayout::eTransferSrcOptimal,
		},
		//eTransferDest
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlagBits::eTransferWrite,
			vk::ImageLayout::eTransferDstOptimal,
		},
		//eVertexRead
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eVertexShader,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eShaderReadOnlyOptimal,
		},
		//eVertexWrite
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eVertexShader,
			vk::AccessFlagBits::eShaderWrite,
			vk::ImageLayout::eGeneral,
		},
		//eFragmentRead
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eFragmentShader,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eShaderReadOnlyOptimal,
		},
		//eFragmentWrite
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eFragmentShader,
			vk::AccessFlagBits::eShaderWrite,
			vk::ImageLayout::eGeneral,
		},
		//eComputeRead
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eComputeShader,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eShaderReadOnlyOptimal,
		},
		//eComputeWrite
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eComputeShader,
			vk::AccessFlagBits::eShaderWrite,
			vk::ImageLayout::eGeneral,
		},
		//eColorAttachmentOutput
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead,
			vk::ImageLayout::eColorAttachmentOptimal,
		},
		//eDepthStencilAttachment,
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eLateFragmentTests | vk::PipelineStageFlagBits::eEarlyFragmentTests,
			vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
		},
		//eDepthStencilReadonly,
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eLateFragmentTests | vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eFragmentShader,
			vk::AccessFlagBits::eDepthStencilAttachmentRead,
			vk::ImageLayout::eDepthStencilReadOnlyOptimal,
		},
		//ePresent
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eAllCommands,
			vk::AccessFlagBits::eNone,
			vk::ImageLayout::ePresentSrcKHR,
		},
		//eVertexAttribute
		ResourceUsageVulkanInfo{
			vk::PipelineStageFlagBits::eVertexInput,
			vk::AccessFlagBits::eVertexAttributeRead,
			vk::ImageLayout::eGeneral,
		},
	};


	ResourceUsageVulkanInfo Combination(ResourceUsageVulkanInfo const& infoA
		, ResourceUsageVulkanInfo const& infoB)
	{
		ResourceUsageVulkanInfo result{};
		result.m_UsageStageMask = infoA.m_UsageStageMask | infoB.m_UsageStageMask;
		result.m_UsageAccessFlags = infoA.m_UsageAccessFlags | infoB.m_UsageAccessFlags;
		result.m_UsageImageLayout = (infoA.m_UsageImageLayout == infoB.m_UsageImageLayout)
			? infoA.m_UsageImageLayout : vk::ImageLayout::eGeneral;
		return result;
	}

	const ResourceUsageVulkanInfo GetUsageInfo(ResourceUsageFlags usageFlags)
	{
		if (!usageFlags)
		{
			return s_NoneUsageVulkanInfo;
		}
		ResourceUsageVulkanInfo result{};
		bool init = false;
		for (uint32_t id = 0; id < EResourceUsageIDs::eMax; ++id)
		{
			if ((usageFlags.m_mask & (1 << id)) != 0)
			{
				if (!init)
				{
					init = true;
					result = s_ResourceUsageToVulkanInfo[id];
				}
				else
				{
					result = Combination(result, s_ResourceUsageToVulkanInfo[id]);
				}
			}
		}
		return result;
	}
}