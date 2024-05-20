#pragma once
#include <Common.h>
#include "VulkanApplicationSubobjectBase.h"
#include "RenderBackendSettings.h"

namespace graphics_backend
{
	struct QueueFamilyInfo
	{
		int m_FamilyIndex;
		int m_FamilyQueueCount;
	};

	struct CommandBatch
	{
		castl::vector<vk::CommandBuffer> commandBuffers;
		int dependOnBatchIndex = -1;
	};

	enum class QueueType
	{
		eGraphics = 0,
		eCompute,
		eTransfer,
	};
	constexpr size_t QUEUE_TYPE_COUNT = 3;
	class QueueContext : public VKAppSubObjectBaseNoCopy
	{
	public:
		QueueContext(CVulkanApplication& app);

		struct QueueCreationInfo
		{
			castl::vector<castl::vector<float>> queueProities;
			castl::vector<vk::DeviceQueueCreateInfo> queueCreateInfoList;
		};

		void InitQueueCreationInfo(vk::PhysicalDevice phyDevice, QueueContext::QueueCreationInfo& outCreationInfo);
		void Release();

		void SubmitCommands(int familyIndex, int queueIndex
			, vk::ArrayProxyNoTemporaries<const vk::CommandBuffer> commandbuffers
			, vk::Fence fence = {}
			, vk::ArrayProxyNoTemporaries<const vk::Semaphore> waitSemaphores = {}
			, vk::ArrayProxyNoTemporaries<const vk::PipelineStageFlags> waitStages = {}
			, vk::ArrayProxyNoTemporaries<const vk::Semaphore> signalSemaphores = {});
		
		int FindPresentQueueFamily(vk::SurfaceKHR surface) const;

		constexpr int GetGraphicsQueueFamily() const
		{
			return m_GraphicsQueueFamilyIndex;
		}

		constexpr int GetTransferQueueFamily() const
		{
			return m_TransferQueueFamilyIndex;
		}

		constexpr int GetComputeQueueFamily() const
		{
			return m_ComputeQueueFamilyIndex;
		}

	private:
		int m_GraphicsQueueFamilyIndex = -1;
		int m_ComputeQueueFamilyIndex = -1;
		int m_TransferQueueFamilyIndex = -1;
		castl::vector<QueueFamilyInfo> m_QueueFamilyList;
	};
}