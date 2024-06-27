#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CAThreadSafeQueue.h>
#include <GPUResources/GPUResourceInternal.h>
#include "CommandBuffersPool.h"
#include "GPUMemoryManager.h"
#include "ResourceReleaseQueue.h"
#include "GPUResourceObjectManager.h"
#include "GraphExecutorManager.h"
#include <DescriptorAllocation/DescriptorLayoutPool.h>
#include <FramebufferObject.h>
#include <CASTL/CAMutex.h>

namespace graphics_backend
{
	class FrameBoundResourcePool : public VKAppSubObjectBaseNoCopy
	{
	public:
		FrameBoundResourcePool(CVulkanApplication& app);
		FrameBoundResourcePool(FrameBoundResourcePool&& other) noexcept;
		void Initialize();
		void Release();
		void ResetPool();
		void HostFinish();
		vk::Fence GetFence() const { return m_Fence; }
		VKBufferObject CreateStagingBuffer(size_t size, EBufferUsageFlags usages, castl::string const& name = "Staging Buffer");
		VKBufferObject CreateBufferWithMemory(GPUBufferDescriptor const& bufferDesc
			, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, const char* name = "");
		VKImageObject CreateImageWithMemory(GPUTextureDescriptor const& textureDesc
			, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal);
		GPUGraphExecutor* NewExecutor(castl::shared_ptr<GPUGraph> const& gpuGraph);

		void FinalizeSubmit();
		void AddLeafSempahores(vk::ArrayProxy<vk::Semaphore> semaphores);
	public:
		CommandBufferThreadPool commandBufferThreadPool;
		GPUMemoryResourceManager memoryManager;
		GPUResourceObjectManager resourceObjectManager;
		FramebufferObjectDic framebufferObjectCache;
		GlobalResourceReleaseQueue releaseQueue;
		DescriptorPoolDic descriptorPools;
		SemaphorePool semaphorePool;
	private:
		vk::Fence m_Fence;
		castl::mutex m_Mutex;

		GraphExecutorManager m_GraphExecutorManager;

		castl::vector<vk::Semaphore> m_LeafSemaphores;
		castl::vector<vk::PipelineStageFlags> m_LeafStageFlags;

		static_assert(std::move_constructible<CommandBufferThreadPool>, "CommandBufferThreadPool Shoule Be Movable");
		static_assert(std::move_constructible<GPUMemoryResourceManager>, "GPUMemoryResourceManager Shoule Be Movable");
		static_assert(std::move_constructible<GPUResourceObjectManager>, "GPUResourceObjectManager Shoule Be Movable");
		static_assert(std::move_constructible<GlobalResourceReleaseQueue>, "GlobalResourceReleaseQueue Shoule Be Movable");
		static_assert(std::move_constructible<DescriptorPoolDic>, "DescriptorPoolDic Shoule Be Movable");
		static_assert(std::move_constructible<SemaphorePool>, "SemaphorePool Shoule Be Movable");
		static_assert(std::move_constructible<GraphExecutorManager>, "GraphExecutorManager Shoule Be Movable");
		static_assert(std::move_constructible<FramebufferObjectDic>, "FramebufferObjectDic Shoule Be Movable");
	};
}