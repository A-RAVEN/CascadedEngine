#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CAThreadSafeQueue.h>
#include <GPUResources/GPUResourceInternal.h>
#include "CommandBuffersPool.h"
#include "GPUMemoryManager.h"
#include "ResourceReleaseQueue.h"
#include "GPUResourceObjectManager.h"
#include <DescriptorAllocation/DescriptorLayoutPool.h>

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
		VKBufferObject CreateStagingBuffer(size_t size, EBufferUsageFlags usages);
		VKBufferObject CreateBufferWithMemory(GPUBufferDescriptor const& bufferDesc
			, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal);
		VKImageObject CreateImageWithMemory(GPUTextureDescriptor const& textureDesc
			, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal);
	public:
		CommandBufferThreadPool commandBufferThreadPool;
		GPUMemoryResourceManager memoryManager;
		GPUResourceObjectManager resourceObjectManager;
		GlobalResourceReleaseQueue releaseQueue;
		DescriptorPoolDic descriptorPools;
		SemaphorePool semaphorePool;
	private:
		vk::Fence m_Fence;

		static_assert(std::move_constructible<CommandBufferThreadPool>, "CommandBufferThreadPool Shoule Be Movable");
		static_assert(std::move_constructible<GPUMemoryResourceManager>, "GPUMemoryResourceManager Shoule Be Movable");
		static_assert(std::move_constructible<GPUResourceObjectManager>, "GPUResourceObjectManager Shoule Be Movable");
		static_assert(std::move_constructible<GlobalResourceReleaseQueue>, "GlobalResourceReleaseQueue Shoule Be Movable");
		static_assert(std::move_constructible<DescriptorPoolDic>, "DescriptorPoolDic Shoule Be Movable");
		static_assert(std::move_constructible<SemaphorePool>, "SemaphorePool Shoule Be Movable");
	};
}