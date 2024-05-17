#include "QueueContext.h"

namespace graphics_backend
{
	QueueContext::QueueContext(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}
	QueueContext::QueueCreationInfo QueueContext::Init()
	{
		QueueCreationInfo result{};
		auto phyDevice = GetPhysicalDevice();

		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = phyDevice.getQueueFamilyProperties();

		vk::QueueFlags generalFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
		for (uint32_t familyId = 0; familyId < queueFamilyProperties.size(); ++familyId)
		{
			vk::QueueFamilyProperties const& itrProp = queueFamilyProperties[familyId];
			QueueFamilyInfo queueFamilyInfo{};
			queueFamilyInfo.m_FamilyIndex = familyId;
			queueFamilyInfo.m_FamilyQueueCount = castl::clamp(itrProp.queueCount, 1u, 4u);
			queueFamilyInfo.m_QueueList.reserve(itrProp.queueCount);

			result.queueProities.emplace_back();
			auto& currentQueuePriorities = result.queueProities.back();
			currentQueuePriorities.resize(queueFamilyInfo.m_FamilyQueueCount);
			castl::fill(currentQueuePriorities.begin(), currentQueuePriorities.end(), 0.0f);
			result.queueCreateInfoList.emplace_back(vk::DeviceQueueCreateFlags{}, familyId, currentQueuePriorities);

			if ((itrProp.queueFlags & generalFlags) == generalFlags)
			{
				m_GraphicsQueueFamilyIndex = familyId;
			}
			else
			{
				if (itrProp.queueFlags & vk::QueueFlagBits::eCompute)
				{
					m_ComputeQueueFamilyIndex = familyId;
				}
				else if (itrProp.queueFlags & vk::QueueFlagBits::eTransfer)
				{
					m_TransferQueueFamilyIndex = familyId;
				}
				else
				{
				}
			}
			m_QueueFamilyList.push_back(queueFamilyInfo);
		}
		CA_ASSERT(m_GraphicsQueueFamilyIndex >= 0, "Vulkan: No General Usage Queue Found!");
		return result;
	}
	void QueueContext::SubmitCurrentFrameGraphics(castl::vector<vk::CommandBuffer> const& commandbufferList, vk::ArrayProxyNoTemporaries<const vk::Semaphore> waitSemaphores, vk::ArrayProxyNoTemporaries<const vk::PipelineStageFlags> waitStages, vk::ArrayProxyNoTemporaries<const vk::Semaphore> signalSemaphores)
	{
	}
}