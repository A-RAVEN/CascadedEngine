#include "Platform.h"
#include "FrameBoundResourcePool.h"

namespace graphics_backend
{
	FrameBoundResourcePool::FrameBoundResourcePool(CVulkanApplication& app) :
		VKAppSubObjectBaseNoCopy(app)
		, commandBufferThreadPool(app)
		, resourceManager(app)
		, releaseQueue(app)
		, resourceObjectManager(app)
	{
	}
	void FrameBoundResourcePool::Initialize()
	{
		resourceManager.Initialize();
		vk::FenceCreateInfo info{};
		info.flags = vk::FenceCreateFlagBits::eSignaled;
		m_Fence = GetDevice().createFence(info);
	}
	void FrameBoundResourcePool::Release()
	{
		commandBufferThreadPool.Release();
		resourceManager.Release();
		releaseQueue.ReleaseGlobalResources();
		resourceObjectManager.Release();
		GetDevice().destroyFence(m_Fence);
	}
	void FrameBoundResourcePool::ResetPool()
	{
		GetDevice().waitForFences(m_Fence, true, castl::numeric_limits<uint64_t>::max());
		GetDevice().resetFences(m_Fence);
		commandBufferThreadPool.ResetPool();
		resourceManager.FreeAllMemory();
		releaseQueue.ReleaseGlobalResources();
		resourceObjectManager.DestroyAll();
	}
}