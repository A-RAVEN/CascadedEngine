#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CAThreadSafeQueue.h>
#include <GPUResources/GPUResourceInternal.h>
#include "CommandBuffersPool.h"
#include "GPUMemoryManager.h"
#include "ResourceReleaseQueue.h"
#include "GPUResourceObjectManager.h"

namespace graphics_backend
{
	class FrameBoundResourcePool : public VKAppSubObjectBaseNoCopy
	{
	public:
		FrameBoundResourcePool(CVulkanApplication& app);
		void Initialize();
		void Release();
		void ResetPool();
	public:
		OneTimeCommandBufferPool commandBufferPool;
		GPUMemoryResourceManager resourceManager;
		GlobalResourceReleaseQueue releaseQueue;
		GPUResourceObjectManager resourceObjectManager;
		castl::threadsafe_queue<OneTimeCommandBufferPool*> m_CommandBufferPools;
	};
}