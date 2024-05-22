#include "CommandBuffersPool.h"
#include "FrameBoundResourcePool.h"
#include <GPUContexts/QueueContext.h>
#include <uenum.h>

namespace graphics_backend
{
	OneTimeCommandBufferPool::OneTimeCommandBufferPool(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}
	void OneTimeCommandBufferPool::Initialize()
	{
		auto& queueContext = GetQueueContext();
		int queueFamily = queueContext.GetGraphicsQueueFamily();
		CA_ASSERT(queueFamily != -1, "Graphics queue family not found");
		if (queueFamily != -1)
		{
			vk::CommandPoolCreateInfo poolInfo(vk::CommandPoolCreateFlagBits::eTransient, queueFamily);
			SubCommandBufferPool newPool;
			newPool.m_CommandPool = GetDevice().createCommandPool(poolInfo);
			m_CommandBufferPools.push_back(newPool);
			m_QueueTypeToPoolIndex[uenum::enumToInt(QueueType::eGraphics)] = m_CommandBufferPools.size() - 1;
		}
		castl::fill(m_QueueTypeToPoolIndex.begin(), m_QueueTypeToPoolIndex.end(), queueFamily);

		queueFamily = queueContext.GetComputeQueueFamily();
		if (queueFamily != -1)
		{
			vk::CommandPoolCreateInfo poolInfo(vk::CommandPoolCreateFlagBits::eTransient, queueFamily);
			SubCommandBufferPool newPool;
			newPool.m_CommandPool = GetDevice().createCommandPool(poolInfo);
			m_CommandBufferPools.push_back(newPool);
			m_QueueTypeToPoolIndex[uenum::enumToInt(QueueType::eCompute)] = m_CommandBufferPools.size() - 1;
		}
		queueFamily = queueContext.GetTransferQueueFamily();
		if (queueFamily != -1)
		{
			vk::CommandPoolCreateInfo poolInfo(vk::CommandPoolCreateFlagBits::eTransient, queueFamily);
			SubCommandBufferPool newPool;
			newPool.m_CommandPool = GetDevice().createCommandPool(poolInfo);
			m_CommandBufferPools.push_back(newPool);
			m_QueueTypeToPoolIndex[uenum::enumToInt(QueueType::eTransfer)] = m_CommandBufferPools.size() - 1;
		}
	}
	void OneTimeCommandBufferPool::Release()
	{
		for (auto& pool : m_CommandBufferPools)
		{
			pool.Release(GetDevice());
		}
		m_CommandBufferPools.clear();
	}

	vk::CommandBuffer OneTimeCommandBufferPool::AllocCommand(QueueType queueType, const char* cmdName)
	{
		return m_CommandBufferPools[m_QueueTypeToPoolIndex[uenum::enumToInt(queueType)]].AllocPrimaryCommandBuffer(GetDevice(), cmdName);
	}

	vk::CommandBuffer OneTimeCommandBufferPool::AllocSecondaryCommand(const char* cmdName)
	{
		return m_CommandBufferPools[m_QueueTypeToPoolIndex[uenum::enumToInt(QueueType::eGraphics)]].AllocSecondaryCommandBuffer(GetDevice(), cmdName);
	}


	void OneTimeCommandBufferPool::ResetCommandBufferPool()
	{
		for (auto& pool : m_CommandBufferPools)
		{
			pool.ResetCommandBufferPool(GetDevice());
		}
	}

	void OneTimeCommandBufferPool::SubCommandBufferPool::ResetCommandBufferPool(vk::Device device)
	{
		device.resetCommandPool(m_CommandPool, vk::CommandPoolResetFlagBits::eReleaseResources);
		m_PrimaryCommandBuffers.Reset();
		m_SecondaryCommandBuffers.Reset();
	}

	void OneTimeCommandBufferPool::SubCommandBufferPool::Release(vk::Device device)
	{
		m_PrimaryCommandBuffers.Release();
		m_SecondaryCommandBuffers.Release();
		device.destroyCommandPool(m_CommandPool);
	}

	vk::CommandBuffer OneTimeCommandBufferPool::SubCommandBufferPool::AllocPrimaryCommandBuffer(vk::Device device, const char* cmdName)
	{
		return AllocateOnetimeCommandBufferInternal(device, cmdName, m_PrimaryCommandBuffers, vk::CommandBufferLevel::ePrimary);
	}

	vk::CommandBuffer OneTimeCommandBufferPool::SubCommandBufferPool::AllocSecondaryCommandBuffer(vk::Device device, const char* cmdName)
	{
		return AllocateOnetimeCommandBufferInternal(device, cmdName, m_SecondaryCommandBuffers, vk::CommandBufferLevel::eSecondary);
	}


	vk::CommandBuffer OneTimeCommandBufferPool::SubCommandBufferPool::AllocateOnetimeCommandBufferInternal(
		vk::Device device
		, const char* cmdName
		, CommandBufferList& manageList
		, vk::CommandBufferLevel commandLevel)
	{
		vk::CommandBuffer result;
		if (!manageList.TryGetNextCommandBuffer(result))
		{
			result = device
				.allocateCommandBuffers(
					vk::CommandBufferAllocateInfo(m_CommandPool
						, commandLevel
						, 1u)).front();
			manageList.AddNewCommandBuffer(result);
		}
		return result;
	}

	castl::shared_ptr<OneTimeCommandBufferPool> CommandBufferThreadPool::AquireCommandBufferPool()
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		OneTimeCommandBufferPool* resultPool = nullptr;
		if (m_AvailablePools.empty())
		{
			m_
		}

		return castl::shared_ptr<OneTimeCommandBufferPool>(resultPool, [this](OneTimeCommandBufferPool* released)
			{
				m_CommandBufferPools.enqueue(released);
			});
	}

	void CommandBufferThreadPool::ResetPool()
	{
		m_CommandBufferPools.
	}

}