#include "ResourceReleaseQueue.h"
#include "GPUMemoryManager.h"
#include "GPUResourceObjectManager.h"

namespace graphics_backend
{
	GlobalResourceReleaseQueue::GlobalResourceReleaseQueue(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}
	GlobalResourceReleaseQueue::GlobalResourceReleaseQueue(GlobalResourceReleaseQueue&& other) noexcept : VKAppSubObjectBaseNoCopy(castl::move(other))
	{
		castl::lock_guard<castl::mutex> otherLock(other.m_Mutex);
		m_PendingBuffers = castl::move(other.m_PendingBuffers);
		m_PendingImages = castl::move(other.m_PendingImages);
	}
	void GlobalResourceReleaseQueue::ReleaseGlobalResources()
	{
		castl::lock_guard<castl::mutex> thisLock(m_Mutex);
		for (auto& buffer : m_PendingBuffers)
		{
			GetGlobalResourceObjectManager().DestroyBuffer(buffer.buffer);
			GetGlobalMemoryManager().FreeMemory(buffer.allocation);
		}
		m_PendingBuffers.clear();
		for (auto& image : m_PendingImages)
		{
			GetGlobalResourceObjectManager().DestroyImage(image.image);
			GetGlobalMemoryManager().FreeMemory(image.allocation);
		}
		m_PendingImages.clear();
		for (auto& swapchain : m_PendingSwapchains)
		{
			swapchain.Release();
		}
		m_PendingSwapchains.clear();
		for (auto& pair : m_PendingSurfaceAndWindows)
		{
			GetInstance().destroySurfaceKHR(pair.second);
			pair.first.reset();
		}
		m_PendingSurfaceAndWindows.clear();
	}
	void GlobalResourceReleaseQueue::AddBuffers(castl::array_ref<VKBufferObject> const& bufferList)
	{
		castl::lock_guard<castl::mutex> thisLock(m_Mutex);
		m_PendingBuffers.reserve(m_PendingBuffers.size() + bufferList.size());
		for(VKBufferObject const& obj : bufferList)
		{
			m_PendingBuffers.push_back(obj);
		}
	}
	void GlobalResourceReleaseQueue::AddImages(castl::array_ref<VKImageObject> const& imgList)
	{
		castl::lock_guard<castl::mutex> thisLock(m_Mutex);
		m_PendingImages.reserve(m_PendingImages.size() + imgList.size());
		for (VKImageObject const& obj : imgList)
		{
			m_PendingImages.push_back(obj);
		}
	}
	void GlobalResourceReleaseQueue::AddSwapchains(castl::array_ref<SwapchainContext> const& swapchains)
	{
		castl::lock_guard<castl::mutex> thisLock(m_Mutex);
		m_PendingSwapchains.reserve(m_PendingSwapchains.size() + swapchains.size());
		for (SwapchainContext const& obj : swapchains)
		{
			m_PendingSwapchains.push_back(obj);
		}
	}
	void GlobalResourceReleaseQueue::AddWindowAndSurface(castl::shared_ptr<cawindow::IWindow> const& windowPtr, vk::SurfaceKHR surface)
	{
		castl::lock_guard<castl::mutex> thisLock(m_Mutex);
		m_PendingSurfaceAndWindows.push_back(castl::make_pair(windowPtr, surface));
	}
	void GlobalResourceReleaseQueue::Load(GlobalResourceReleaseQueue& other)
	{
		castl::lock_guard<castl::mutex> otherLock(other.m_Mutex);
		castl::lock_guard<castl::mutex> thisLock(m_Mutex);
		m_PendingBuffers = castl::move(other.m_PendingBuffers);
		m_PendingImages = castl::move(other.m_PendingImages);
		m_PendingSwapchains = castl::move(other.m_PendingSwapchains);
		m_PendingSurfaceAndWindows = castl::move(other.m_PendingSurfaceAndWindows);
		other.m_PendingBuffers.clear();
		other.m_PendingImages.clear();
		other.m_PendingSwapchains.clear();
		other.m_PendingSurfaceAndWindows.clear();
	}
}