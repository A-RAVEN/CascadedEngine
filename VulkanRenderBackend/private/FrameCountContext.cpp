#include "pch.h"
#include "RenderBackendSettings.h"
#include "VulkanApplication.h"
#include "FrameCountContext.h"

namespace graphics_backend
{
	void CFrameCountContext::WaitingForCurrentFrame()
	{
		++m_CurrentFrameID;
		TIndex currentIndex = GetCurrentFrameBufferIndex();
		FrameType waitingFrame = m_FenceSubmitFrameIDs[currentIndex];
		std::atomic_thread_fence(std::memory_order_acquire);
		castl::vector<vk::Fence> fences = {
			m_SubmitFrameFences[currentIndex]
		};
		GetVulkanApplication()->GetDevice().waitForFences(fences
			, true
			, std::numeric_limits<uint64_t>::max());
		std::atomic_thread_fence(std::memory_order_release);
		m_LastFinshedFrameID = waitingFrame;
		m_FenceSubmitFrameIDs[currentIndex] = m_CurrentFrameID;
	}

	void CFrameCountContext::SubmitGraphics(
		castl::vector<vk::CommandBuffer> const& commandbufferList,
		vk::ArrayProxyNoTemporaries<const vk::Semaphore> waitSemaphores
		, vk::ArrayProxyNoTemporaries<const vk::PipelineStageFlags> waitStages
		, vk::ArrayProxyNoTemporaries<const vk::Semaphore> signalSemaphores) const
	{
		if (commandbufferList.empty() && waitSemaphores.empty() && signalSemaphores.empty())
			return;
		vk::SubmitInfo const submitInfo(waitSemaphores, waitStages, commandbufferList, signalSemaphores);
		m_GraphicsQueue.submit(submitInfo);
	}

	void CFrameCountContext::FinalizeCurrentFrameGraphics(
		castl::vector<vk::CommandBuffer> const& commandbufferList
	    , vk::ArrayProxyNoTemporaries<const vk::Semaphore> waitSemaphores
		, vk::ArrayProxyNoTemporaries<const vk::PipelineStageFlags> waitStages
		, vk::ArrayProxyNoTemporaries<const vk::Semaphore> signalSemaphores)
	{
		uint32_t currentIndex = GetCurrentFrameBufferIndex();
		vk::Fence currentFrameFence = m_SubmitFrameFences[currentIndex];
		castl::vector<vk::Fence> fences = {
			currentFrameFence
		};
		GetDevice().resetFences(fences);
		vk::SubmitInfo submitInfo(waitSemaphores, waitStages, commandbufferList, signalSemaphores);
		m_GraphicsQueue.submit(submitInfo, currentFrameFence);
	}
	void CFrameCountContext::SubmitCurrentFrameCompute(castl::vector<vk::CommandBuffer> const& commandbufferList)
	{

	}
	void CFrameCountContext::SubmitCurrentFrameTransfer(castl::vector<vk::CommandBuffer> const& commandbufferList)
	{
	}
	//replace all castl::pair with castl::pair in this file
	void CFrameCountContext::InitializeSubmitQueues(
		castl::pair<uint32_t, uint32_t> const& generalQueue
		, castl::pair<uint32_t, uint32_t> const& computeQueue
		, castl::pair<uint32_t, uint32_t> const& transferQueue)
	{
		m_GraphicsQueueReference = generalQueue;
		m_ComputeQueueReference = computeQueue;
		m_TransferQueueReference = transferQueue;
		m_GraphicsQueue = GetDevice().getQueue(m_GraphicsQueueReference.first, m_GraphicsQueueReference.second);
		m_ComputeQueue = GetDevice().getQueue(m_ComputeQueueReference.first, m_ComputeQueueReference.second);
		m_TransferQueue = GetDevice().getQueue(m_TransferQueueReference.first, m_TransferQueueReference.second);
	}

	void CFrameCountContext::InitializeDefaultQueues(castl::vector<vk::Queue> defaultQueues)
	{
		m_QueueFamilyDefaultQueues = defaultQueues;
	}

	uint32_t CFrameCountContext::FindPresentQueueFamily(vk::SurfaceKHR surface) const
	{
		for(uint32_t familyId = 0; familyId < m_QueueFamilyDefaultQueues.size(); ++familyId)
		{
			if(GetPhysicalDevice().getSurfaceSupportKHR(familyId, surface))
			{
				return familyId;
			}
		}
		return std::numeric_limits<uint32_t>::max();
	}

	castl::pair<uint32_t, vk::Queue> CFrameCountContext::FindPresentQueue(vk::SurfaceKHR surface) const
	{
		for (uint32_t familyId = 0; familyId < m_QueueFamilyDefaultQueues.size(); ++familyId)
		{
			if (GetPhysicalDevice().getSurfaceSupportKHR(familyId, surface))
			{
				return castl::make_pair(familyId, m_QueueFamilyDefaultQueues[familyId]);
			}
		}
		assert(false);
		return castl::pair<uint32_t, vk::Queue>(INVALID_INDEX, nullptr);
	}

	void CFrameCountContext::Initialize_Internal(CVulkanApplication const* owningApplication)
	{

		assert(m_SubmitFrameFences.empty());
		m_CurrentFrameID = 0;
		m_SubmitFrameFences.reserve(SWAPCHAIN_BUFFER_COUNT);
		for (uint32_t itrFenceId = 0; itrFenceId < SWAPCHAIN_BUFFER_COUNT; ++itrFenceId)
		{
			vk::FenceCreateInfo fenceCreateInfo(vk::FenceCreateFlagBits::eSignaled);
			vk::Fence && newFence = m_OwningApplication->GetDevice().createFence(fenceCreateInfo);
			m_SubmitFrameFences.push_back(newFence);
		}
		m_FenceSubmitFrameIDs.resize(SWAPCHAIN_BUFFER_COUNT);
		castl::fill(m_FenceSubmitFrameIDs.begin(), m_FenceSubmitFrameIDs.end(), INVALID_FRAMEID);
	}

	void CFrameCountContext::Release_Internal()
	{
		for (auto& fence : m_SubmitFrameFences)
		{
			m_OwningApplication->GetDevice().destroyFence(fence);
		}
		m_SubmitFrameFences.clear();
		m_FenceSubmitFrameIDs.clear();
		m_GraphicsQueue = nullptr;
		m_ComputeQueue = nullptr;
		m_TransferQueue = nullptr;
	}
}

