#include "pch.h"
#include "RenderBackendSettings.h"
#include "VulkanApplication.h"
#include "CVulkanThreadContext.h"

namespace graphics_backend
{
	CVulkanFrameBoundCommandBufferPool::CVulkanFrameBoundCommandBufferPool(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}
	vk::CommandBuffer CVulkanFrameBoundCommandBufferPool::AllocateOnetimeCommandBuffer(const char* cmdName)
	{
		vk::CommandBuffer const result = m_CommandBufferList.AllocCommandBuffer(*this, false, cmdName);
		result.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		return result;
	}
	vk::CommandBuffer CVulkanFrameBoundCommandBufferPool::AllocateMiscCommandBuffer(const char* cmdName)
	{
		auto result = AllocateOnetimeCommandBuffer(cmdName);
		m_MiscCommandBufferList.push_back(result);
		return result;
	}
	vk::CommandBuffer CVulkanFrameBoundCommandBufferPool::AllocateSecondaryCommandBuffer(const char* cmdName)
	{
		return m_SecondaryCommandBufferList.AllocCommandBuffer(*this, true, cmdName);
	}
	void CVulkanFrameBoundCommandBufferPool::ResetCommandBufferPool()
	{
		GetDevice().resetCommandPool(m_CommandPool, vk::CommandPoolResetFlagBits::eReleaseResources);
		m_CommandBufferList.ResetBufferList();
		m_SecondaryCommandBufferList.ResetBufferList();
		m_MiscCommandBufferList.clear();
	}
	void CVulkanFrameBoundCommandBufferPool::CollectCommandBufferList(castl::vector<vk::CommandBuffer>& inoutCommandBufferList)
	{
		size_t commandCount = m_MiscCommandBufferList.size();
		if (commandCount > 0)
		{
			inoutCommandBufferList.resize(inoutCommandBufferList.size() + commandCount);
			castl::copy(m_MiscCommandBufferList.begin()
				, m_MiscCommandBufferList.end()
				, inoutCommandBufferList.end() - commandCount);
			m_MiscCommandBufferList.clear();
		}
	}



	void CVulkanFrameBoundCommandBufferPool::Initialize()
	{
		vk::CommandPoolCreateInfo commandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient, 0);
		m_CommandPool = GetDevice().createCommandPool(commandPoolCreateInfo);
		m_CommandBufferList.ClearBufferList();
		m_SecondaryCommandBufferList.ClearBufferList();
	}

	void CVulkanFrameBoundCommandBufferPool::Release()
	{
		m_CommandBufferList.ClearBufferList();
		m_SecondaryCommandBufferList.ClearBufferList();
		GetDevice().destroyCommandPool(m_CommandPool);
		m_CommandPool = nullptr;
	}

	CVulkanThreadContext::CVulkanThreadContext(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app), m_ThreadID(0)
	{}

	CVulkanFrameBoundCommandBufferPool& CVulkanThreadContext::GetCurrentFramePool()
	{
		uint32_t currentFrameId = GetFrameCountContext().GetCurrentFrameBufferIndex();
		return m_FrameBoundCommandBufferPools[currentFrameId];
	}

	CVulkanFrameBoundCommandBufferPool& CVulkanThreadContext::GetPoolByIndex(TIndex poolID)
	{
		return m_FrameBoundCommandBufferPools[poolID];
	}

	void CVulkanThreadContext::DoReleaseContextResourceByIndex(TIndex releasingIndex)
	{
		GetPoolByIndex(releasingIndex).ResetCommandBufferPool();
	}

	void CVulkanThreadContext::Initialize(uint32_t contextID)
	{
		m_ThreadID = contextID;
		assert(m_FrameBoundCommandBufferPools.size() == 0);
		m_FrameBoundCommandBufferPools.reserve(FRAMEBOUND_RESOURCE_POOL_SWAP_COUNT_PER_CONTEXT);
		for(uint32_t i = 0; i < FRAMEBOUND_RESOURCE_POOL_SWAP_COUNT_PER_CONTEXT; ++i)
		{
			m_FrameBoundCommandBufferPools.push_back(castl::move(GetVulkanApplication().NewSubObject<CVulkanFrameBoundCommandBufferPool>()));
		}
		//castl::for_each(m_FrameBoundCommandBufferPools.begin(), m_FrameBoundCommandBufferPools.end()
		//	, [owningApplication](CVulkanFrameBoundCommandBufferPool& itrPool)
		//	{
		//		itrPool.Initialize();
		//	});
	}
	void CVulkanThreadContext::Release()
	{
		if (!m_FrameBoundCommandBufferPools.empty())
		{
			castl::for_each(m_FrameBoundCommandBufferPools.begin(), m_FrameBoundCommandBufferPools.end()
				, [](CVulkanFrameBoundCommandBufferPool& itrPool)
				{
					itrPool.Release();
				});
		}
	}
	vk::CommandBuffer CVulkanFrameBoundCommandBufferPool::CommandBufferList::AllocCommandBuffer(CVulkanFrameBoundCommandBufferPool& owner, bool secondary, const char* cmdName)
	{
		vk::CommandBuffer result = nullptr;
		if (m_AvailableCommandBufferIndex < m_CommandBufferList.size())
		{
			size_t newCmdId = m_AvailableCommandBufferIndex;
			result = m_CommandBufferList[newCmdId];
		}
		else
		{
			result = owner.GetDevice()
				.allocateCommandBuffers(
					vk::CommandBufferAllocateInfo(owner.m_CommandPool
						, secondary ? vk::CommandBufferLevel::eSecondary : vk::CommandBufferLevel::ePrimary
						, 1u)).front();

			m_CommandBufferList.push_back(result);
		}
		++m_AvailableCommandBufferIndex;

#if !defined(NDEBUG)
		VkCommandBuffer c_handle = result;
		uint64_t handle = reinterpret_cast<uint64_t>(c_handle);
		vk::DebugUtilsObjectNameInfoEXT debugName(result.objectType, handle, cmdName);
		owner.GetDevice().setDebugUtilsObjectNameEXT(debugName);
#endif
		return result;
	}

	//TODO Remove!!!
	void CVulkanFrameBoundCommandBufferPool::CommandBufferList::CollectCommandBufferList(castl::vector<vk::CommandBuffer>& inoutCommandBufferList)
	{
		for (int i = 0; i < m_AvailableCommandBufferIndex; ++i)
		{
			inoutCommandBufferList.push_back(m_CommandBufferList[i]);
		}
	}
	void CVulkanFrameBoundCommandBufferPool::CommandBufferList::ResetBufferList()
	{
		m_AvailableCommandBufferIndex = 0;
	}
	void CVulkanFrameBoundCommandBufferPool::CommandBufferList::ClearBufferList()
	{
		m_AvailableCommandBufferIndex = 0;
		m_CommandBufferList.clear();
	}
}
