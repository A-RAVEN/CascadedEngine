#include "Platform.h"
#include "FrameBoundResourcePool.h"

namespace graphics_backend
{
	FrameBoundResourcePool::FrameBoundResourcePool(CVulkanApplication& app) :
		VKAppSubObjectBaseNoCopy(app)
		, commandBufferThreadPool(app)
		, memoryManager(app)
		, releaseQueue(app)
		, resourceObjectManager(app)
		, descriptorPools(app)
		, semaphorePool(app)
	{
	}
	void FrameBoundResourcePool::Initialize()
	{
		memoryManager.Initialize();
		vk::FenceCreateInfo info{};
		info.flags = vk::FenceCreateFlagBits::eSignaled;
		m_Fence = GetDevice().createFence(info);
	}
	void FrameBoundResourcePool::Release()
	{
		commandBufferThreadPool.ReleasePool();
		memoryManager.Release();
		releaseQueue.ReleaseGlobalResources();
		resourceObjectManager.Release();
		GetDevice().destroyFence(m_Fence);
		descriptorPools.Clear();
		semaphorePool.Release();
	}
	void FrameBoundResourcePool::ResetPool()
	{
		GetDevice().waitForFences(m_Fence, true, castl::numeric_limits<uint64_t>::max());
		GetDevice().resetFences(m_Fence);
		commandBufferThreadPool.ResetPool();
		memoryManager.FreeAllMemory();
		releaseQueue.ReleaseGlobalResources();
		resourceObjectManager.DestroyAll();
		descriptorPools.Foreach([&](auto& desc, DescriptorPool* pool) { pool->ResetAll(); });
		semaphorePool.Reset();
	}
	VKBufferObject FrameBoundResourcePool::CreateStagingBuffer(size_t size, EBufferUsageFlags usages)
	{
		VKBufferObject result{};
		result.buffer = resourceObjectManager.CreateBuffer(GPUBufferDescriptor::Create(usages, 1, size));
		result.allocation = memoryManager.AllocateMemory(result.buffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		return result;
	}
	VKBufferObject FrameBoundResourcePool::CreateBufferWithMemory(GPUBufferDescriptor const& bufferDesc, vk::MemoryPropertyFlags memoryFlags)
	{
		VKBufferObject result{};
		result.buffer = resourceObjectManager.CreateBuffer(bufferDesc);
		result.allocation = memoryManager.AllocateMemory(result.buffer, memoryFlags);
		return result;
	}
	VKImageObject FrameBoundResourcePool::CreateImageWithMemory(GPUTextureDescriptor const& textureDesc, vk::MemoryPropertyFlags memoryFlags)
	{
		VKImageObject result{};
		result.image = resourceObjectManager.CreateImage(textureDesc);
		result.allocation = memoryManager.AllocateMemory(result.image, memoryFlags);
		return result;
	}
}