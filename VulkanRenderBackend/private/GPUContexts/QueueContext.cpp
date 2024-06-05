#include "QueueContext.h"

namespace graphics_backend
{
	QueueContext::QueueContext(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}
	void QueueContext::InitQueueCreationInfo(vk::PhysicalDevice phyDevice, QueueContext::QueueCreationInfo& outCreationInfo)
	{
		constexpr vk::PipelineStageFlags graphicsFlags = ~vk::PipelineStageFlags{ 0 };

		constexpr vk::PipelineStageFlags transferFlags
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

		constexpr vk::PipelineStageFlags computeFlags
			= vk::PipelineStageFlagBits::eComputeShader
			| vk::PipelineStageFlagBits::eDrawIndirect
			| vk::PipelineStageFlagBits::eTopOfPipe
			| vk::PipelineStageFlagBits::eBottomOfPipe;

		outCreationInfo.queueCreateInfoList.clear();
		outCreationInfo.queueProities.clear();
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = phyDevice.getQueueFamilyProperties();

		vk::QueueFlags generalFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
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
				CA_LOG_ERR("General Queue Is " + castl::to_string(m_GraphicsQueueFamilyIndex));
			}
			else
			{
				if (itrProp.queueFlags & vk::QueueFlagBits::eCompute)
				{
					m_ComputeQueueFamilyIndex = familyId;
					m_ComputeStageMask = computeFlags;
					CA_LOG_ERR("Compute Queue Is " + castl::to_string(m_ComputeQueueFamilyIndex));
				}
				else if (itrProp.queueFlags & vk::QueueFlagBits::eTransfer)
				{
					m_TransferQueueFamilyIndex = familyId;
					m_TransferStageMask = transferFlags;
					CA_LOG_ERR("Transfer Queue Is " + castl::to_string(m_TransferQueueFamilyIndex));
				}
				else
				{
				}
			}
			m_QueueFamilyList.push_back(queueFamilyInfo);
		}
		CA_ASSERT(m_GraphicsQueueFamilyIndex >= 0, "Vulkan: No General Usage Queue Found!");
	}

	void QueueContext::Release()
	{
		m_GraphicsQueueFamilyIndex = -1;
		m_ComputeQueueFamilyIndex = -1;
		m_TransferQueueFamilyIndex = -1;
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