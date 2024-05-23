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
		vk::Fence GetFence() const { return m_Fence; }
	public:
		CommandBufferThreadPool commandBufferThreadPool;
		GPUMemoryResourceManager resourceManager;
		GPUResourceObjectManager resourceObjectManager;
		GlobalResourceReleaseQueue releaseQueue;
	private:
		vk::Fence m_Fence;
	};
}