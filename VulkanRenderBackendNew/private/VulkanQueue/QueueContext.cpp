#include "QueueContext.h"

namespace graphics_backend
{

	void QueueContext::InitQueueCreationInfo(vk::PhysicalDevice phyDevice, QueueContext::QueueCreationInfo& outCreationInfo)
	{
		constexpr vk::PipelineStageFlags graphicsFlags = ~vk::PipelineStageFlags{ 0 };

		constexpr vk::PipelineStageFlags transferStageMask
			= vk::PipelineStageFlagBits::eTransfer
			| vk::PipelineStageFlagBits::eAllCommands
			| vk::PipelineStageFlagBits::eAllGraphics
			| vk::PipelineStageFlagBits::eTopOfPipe
			| vk::PipelineStageFlagBits::eBottomOfPipe;

		constexpr vk::PipelineStageFlags hostFlags
			= vk::PipelineStageFlagBits::eHost;

		constexpr vk::PipelineStageFlags accelFlags
			= vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR
			| vk::PipelineStageFlagBits::eAccelerationStructureBuildNV;

		constexpr vk::PipelineStageFlags rtFlags = 
			vk::PipelineStageFlagBits::eRayTracingShaderKHR
			| vk::PipelineStageFlagBits::eRayTracingShaderNV
			| vk::PipelineStageFlagBits::eDrawIndirect;

		constexpr vk::PipelineStageFlags computeStageFlags
			= vk::PipelineStageFlagBits::eComputeShader
			| vk::PipelineStageFlagBits::eDrawIndirect
			| vk::PipelineStageFlagBits::eTopOfPipe
			| vk::PipelineStageFlagBits::eBottomOfPipe;

		outCreationInfo.queueCreateInfoList.clear();
		outCreationInfo.queueProities.clear();
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = phyDevice.getQueueFamilyProperties();

		vk::QueueFlags generalFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
		vk::QueueFlags computeFlags = vk::QueueFlagBits::eCompute;
		vk::QueueFlags transferFlags = vk::QueueFlagBits::eTransfer;
		//#if VK_ENABLE_BETA_EXTENSIONS
		vk::QueueFlags videoDecodingFlags = vk::QueueFlagBits::eVideoDecodeKHR;
//#endif
		for (uint32_t familyId = 0; familyId < queueFamilyProperties.size(); ++familyId)
		{
			vk::QueueFamilyProperties const& itrProp = queueFamilyProperties[familyId];
			QueueFamilyInfo queueFamilyInfo{};
			queueFamilyInfo.m_FamilyIndex = familyId;
			queueFamilyInfo.m_FamilyQueueCount = castl::clamp(itrProp.queueCount, 1u, 4u);

			outCreationInfo.queueProities.emplace_back();
			auto& currentQueuePriorities = outCreationInfo.queueProities.back();
			currentQueuePriorities.resize(queueFamilyInfo.m_FamilyQueueCount);
			castl::fill(currentQueuePriorities.begin(), currentQueuePriorities.end(), 0.0f);
			outCreationInfo.queueCreateInfoList.emplace_back(vk::DeviceQueueCreateFlags{}, familyId, currentQueuePriorities);

			if ((itrProp.queueFlags & generalFlags) == generalFlags)
			{
				m_GraphicsQueueFamilyIndex = familyId;
				m_GraphicsStageMask = ~vk::PipelineStageFlags{ 0 };
				CA_LOG("General Family Is {}", m_GraphicsQueueFamilyIndex);
			}
			else if (itrProp.queueFlags & computeFlags)
			{
				m_ComputeQueueFamilyIndex = familyId;
				m_ComputeStageMask = computeStageFlags;
				CA_LOG("Compute Family Is {}", m_ComputeQueueFamilyIndex);
			}
//#if VK_ENABLE_BETA_EXTENSIONS
			else if (itrProp.queueFlags & videoDecodingFlags)
			{
				m_VideoDecodeFamilyIndex = familyId;
				m_VideoDecodeStageMask = transferStageMask;
				CA_LOG("Video Decoding Family Is {}", m_VideoDecodeFamilyIndex);
			}
//#endif
			else if (itrProp.queueFlags & transferFlags)
			{
				m_TransferQueueFamilyIndex = familyId;
				m_TransferStageMask = transferStageMask;
				CA_LOG("Transfer Family Is {}", m_TransferQueueFamilyIndex);
			}
			else
			{
				CA_LOG("UnCategoried Family {}", familyId);
			}
			m_QueueFamilyList.push_back(queueFamilyInfo);
		}

		// Single-universal-family devices (integrated GPUs, most discrete GPUs): the
		// universal family is classified as graphics above, so no family ever matches the
		// compute/transfer branches and both indices stay -1. Fall back to the graphics
		// family — passing -1 downstream converts to 0xFFFFFFFF (uint32) in
		// vkGetDeviceQueue / vkCreateCommandPool, which is an invalid queue family index.
		if (m_ComputeQueueFamilyIndex < 0 && m_GraphicsQueueFamilyIndex >= 0)
		{
			m_ComputeQueueFamilyIndex = m_GraphicsQueueFamilyIndex;
			m_ComputeStageMask = computeStageFlags;
			CA_LOG("No dedicated compute family — falling back to graphics family {}", m_ComputeQueueFamilyIndex);
		}
		if (m_TransferQueueFamilyIndex < 0 && m_GraphicsQueueFamilyIndex >= 0)
		{
			m_TransferQueueFamilyIndex = m_GraphicsQueueFamilyIndex;
			m_TransferStageMask = transferStageMask;
			CA_LOG("No dedicated transfer family — falling back to graphics family {}", m_TransferQueueFamilyIndex);
		}
		CA_ASSERT(m_GraphicsQueueFamilyIndex >= 0, "Vulkan: No General Usage Queue Found!");
		CA_ASSERT(m_ComputeQueueFamilyIndex >= 0 && m_TransferQueueFamilyIndex >= 0,
			"Vulkan: Compute/Transfer queue family must be valid after fallback");
	}

	void QueueContext::Release()
	{
		m_GraphicsQueueFamilyIndex = -1;
		m_ComputeQueueFamilyIndex = -1;
		m_TransferQueueFamilyIndex = -1;
		m_VideoDecodeFamilyIndex = -1;
		m_QueueFamilyList = {};
	}

	void QueueContext::SubmitCommands(int familyIndex, int queueIndex
		, vk::ArrayProxyNoTemporaries<const vk::CommandBuffer> commandbuffers
		, vk::Fence fence
		, vk::ArrayProxyNoTemporaries<const vk::Semaphore> waitSemaphores
		, vk::ArrayProxyNoTemporaries<const vk::PipelineStageFlags> waitStages
		, vk::ArrayProxyNoTemporaries<const vk::Semaphore> signalSemaphores)
	{
		vk::SubmitInfo submitInfo(waitSemaphores, waitStages, commandbuffers, signalSemaphores);
		GetDevice().getQueue(familyIndex, queueIndex).submit(submitInfo, fence);
	}
	bool QueueContext::QueueFamilySupportsPresent(vk::SurfaceKHR surface, int familyIndex) const
	{
		return GetPhysicalDevice().getSurfaceSupportKHR(familyIndex, surface);
	}
	int QueueContext::FindPresentQueueFamily(vk::SurfaceKHR surface) const
	{
		for (auto& queueFamily : m_QueueFamilyList)
		{
			if (QueueFamilySupportsPresent(surface, queueFamily.m_FamilyIndex))
			{
				return queueFamily.m_FamilyIndex;
			}
		}
		return -1;
	}
}