#include "CommandBuffersPool.h"
#include "FrameBoundResourcePool.h"
#include <GPUContexts/QueueContext.h>
#include <uenum.h>

namespace graphics_backend
{
	OneTimeCommandBufferPool::OneTimeCommandBufferPool(CVulkanApplication& app, uint32_t index)
		: VKAppSubObjectBaseNoCopy(app), m_Index(index)
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
		result.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		return result;
	}

	CommandBufferThreadPool::CommandBufferThreadPool(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}

	castl::shared_ptr<OneTimeCommandBufferPool> CommandBufferThreadPool::AquireCommandBufferPool()
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		OneTimeCommandBufferPool* resultPool = nullptr;
		if (m_AvailablePools.empty())
		{
			uint32_t index = m_CommandBufferPools.size();
			m_CommandBufferPools.emplace_back(GetVulkanApplication(), index);
			m_CommandBufferPools.back().Initialize();
			resultPool = &m_CommandBufferPools.back();
		}
		else
		{
			uint32_t index = m_AvailablePools.back();
			m_AvailablePools.pop_back();
			resultPool = &m_CommandBufferPools[index];
		}
		return castl::shared_ptr<OneTimeCommandBufferPool>(resultPool, [this](OneTimeCommandBufferPool* released)
		{
			castl::lock_guard<castl::mutex> lock(m_Mutex);
			CA_ASSERT(released == &m_CommandBufferPools[released->m_Index], "Invalid CommandBuffer Pool");
			m_AvailablePools.push_back(released->m_Index);
		});
	}

	void CommandBufferThreadPool::ResetPool()
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		for(auto& pool : m_CommandBufferPools)
		{
			pool.ResetCommandBufferPool();
		}
	}

	void CommandBufferThreadPool::ReleasePool()
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		for (auto& pool : m_CommandBufferPools)
		{
			pool.Release();
		}
		m_CommandBufferPools.clear();
		m_AvailablePools.clear();
	}

}