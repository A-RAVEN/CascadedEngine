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
		eMax,
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
		
		bool QueueFamilySupportsPresent(vk::SurfaceKHR surface, int familyIndex) const;
		int FindPresentQueueFamily(vk::SurfaceKHR surface) const;

		constexpr int GetGraphicsQueueFamily() const
		{
			return m_GraphicsQueueFamilyIndex;
		}

		constexpr vk::PipelineStageFlags GetGraphicsPipelineStageMask() const
		{
			return m_GraphicsStageMask;
		}


		constexpr int GetTransferQueueFamily() const
		{
			return m_TransferQueueFamilyIndex;
		}


		constexpr vk::PipelineStageFlags GetTransferPipelineStageMask() const
		{
			return m_TransferStageMask;
		}


		constexpr int GetComputeQueueFamily() const
		{
			return m_ComputeQueueFamilyIndex;
		}

		constexpr vk::PipelineStageFlags GetComputePipelineStageMask() const
		{
			return m_ComputeStageMask;
		}

		constexpr QueueType QueueFamilyIndexToQueueType(int queueFamily)
		{
			if (queueFamily == m_GraphicsQueueFamilyIndex)
				return QueueType::eGraphics;
			if (queueFamily == m_ComputeQueueFamilyIndex)
				return QueueType::eCompute;
			if (queueFamily == m_TransferQueueFamilyIndex)
				return QueueType::eTransfer;

			return QueueType::eMax;
		}

		constexpr vk::PipelineStageFlags QueueFamilyIndexToPipelineStageMask(int queueFamily)
		{
			QueueType queueType = GetQueueContext().QueueFamilyIndexToQueueType(queueFamily);
			switch (queueType)
			{
			case QueueType::eGraphics:
				return GetGraphicsPipelineStageMask();
				break;
			case QueueType::eCompute:
				return GetComputePipelineStageMask();
				break;
			case QueueType::eTransfer:
				return GetQueueContext().GetTransferPipelineStageMask();
				break;
			}

			return vk::PipelineStageFlags{ 0 };
		}

	private:
		int m_GraphicsQueueFamilyIndex = -1;
		vk::PipelineStageFlags m_GraphicsStageMask;
		int m_ComputeQueueFamilyIndex = -1;
		vk::PipelineStageFlags m_ComputeStageMask;
		int m_TransferQueueFamilyIndex = -1;
		vk::PipelineStageFlags m_TransferStageMask;
		castl::vector<QueueFamilyInfo> m_QueueFamilyList;
	};
}