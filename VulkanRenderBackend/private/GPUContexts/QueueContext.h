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
		castl::vector<vk::Queue> m_QueueList;
	};

	class QueueContext : public VKAppSubObjectBaseNoCopy
	{
	public:
		QueueContext(CVulkanApplication& app);

		struct QueueCreationInfo
		{
			castl::vector<castl::vector<float>> queueProities;
			castl::vector<vk::DeviceQueueCreateInfo> queueCreateInfoList;
		};

		QueueCreationInfo Init();
		void Release();

		void SubmitCurrentFrameGraphics(castl::vector<vk::CommandBuffer> const& commandbufferList
			, vk::ArrayProxyNoTemporaries<const vk::Semaphore> waitSemaphores = {}
			, vk::ArrayProxyNoTemporaries<const vk::PipelineStageFlags> waitStages = {}
			, vk::ArrayProxyNoTemporaries<const vk::Semaphore> signalSemaphores = {});

		void SubmitCurrentFrameCompute(castl::vector<vk::CommandBuffer> const& commandbufferList);
		void SubmitCurrentFrameTransfer(castl::vector<vk::CommandBuffer> const& commandbufferList);
		void InitializeSubmitQueues(
			castl::vector<vk::Queue> defaultQueues
			, castl::pair<uint32_t, uint32_t> const& generalQueue
			, castl::pair<uint32_t, uint32_t> const& computeQueue
			, castl::pair<uint32_t, uint32_t> const& transferQueue);
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