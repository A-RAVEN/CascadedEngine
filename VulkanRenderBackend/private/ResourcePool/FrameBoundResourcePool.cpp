#include "FrameBoundResourcePool.h"

namespace graphics_backend
{
	FrameBoundResourcePool::FrameBoundResourcePool(CVulkanApplication& app) :
		VKAppSubObjectBaseNoCopy(app)
		, commandBufferPool(app)
		, resourceManager(app)
		, releaseQueue(app)
		, resourceObjectManager(app)
	{
	}
	void FrameBoundResourcePool::Initialize()
	{
		commandBufferPool.Initialize();
		resourceManager.Initialize();
	}
	void FrameBoundResourcePool::Release()
	{
		commandBufferPool.Release();
		resourceManager.Release();
		releaseQueue.ReleaseGlobalResources();
		resourceObjectManager.Release();
	}
	void FrameBoundResourcePool::ResetPool()
	{
		commandBufferPool.ResetCommandBufferPool();
		resourceManager.FreeAllMemory();
		releaseQueue.ReleaseGlobalResources();
		resourceObjectManager.DestroyAll();
	}
}