#include "Platform.h"
#include "FrameBoundResourcePool.h"
#include <VulkanDebug.h>

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
	FrameBoundResourcePool::FrameBoundResourcePool(FrameBoundResourcePool&& other) noexcept : 
		VKAppSubObjectBaseNoCopy(other.GetVulkanApplication())
		, commandBufferThreadPool(castl::move(other.commandBufferThreadPool))
		, memoryManager(castl::move(other.memoryManager))
		, releaseQueue(castl::move(other.releaseQueue))
		, resourceObjectManager(castl::move(other.resourceObjectManager))
		, descriptorPools(castl::move(other.descriptorPools))
		, semaphorePool(castl::move(other.semaphorePool))
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
		m_Mutex.lock();
		GetDevice().waitForFences(m_Fence, true, castl::numeric_limits<uint64_t>::max());
		GetDevice().resetFences(m_Fence);
		commandBufferThreadPool.ResetPool();
		memoryManager.FreeAllMemory();
		releaseQueue.ReleaseGlobalResources();
		resourceObjectManager.DestroyAll();
		descriptorPools.Foreach([&](auto& desc, DescriptorPool* pool) { pool->ResetAll(); });
		semaphorePool.Reset();
	}
	void FrameBoundResourcePool::HostFinish()
	{
		m_Mutex.unlock();
	}
	VKBufferObject FrameBoundResourcePool::CreateStagingBuffer(size_t size, EBufferUsageFlags usages, castl::string const& name)
	{
		return CreateBufferWithMemory(GPUBufferDescriptor::Create(usages, size, 1), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, name.c_str());
	}
	VKBufferObject FrameBoundResourcePool::CreateBufferWithMemory(GPUBufferDescriptor const& bufferDesc, vk::MemoryPropertyFlags memoryFlags, const char* name)
	{
		VKBufferObject result{};
		result.buffer = resourceObjectManager.CreateBuffer(bufferDesc);
		SetVKObjectDebugName(GetDevice(), result.buffer, name);
		result.allocation = memoryManager.AllocateMemory(result.buffer, memoryFlags);
		memoryManager.BindMemory(result.buffer, result.allocation);
		return result;
	}
	VKImageObject FrameBoundResourcePool::CreateImageWithMemory(GPUTextureDescriptor const& textureDesc, vk::MemoryPropertyFlags memoryFlags)
	{
		VKImageObject result{};
		result.image = resourceObjectManager.CreateImage(textureDesc);
		result.allocation = memoryManager.AllocateMemory(result.image, memoryFlags);
		memoryManager.BindMemory(result.image, result.allocation);
		return result;
	}
	void FrameBoundResourcePool::FinalizeSubmit()
	{
		GetQueueContext()
			.SubmitCommands(GetQueueContext().GetGraphicsQueueFamily()
				, 0
				, {}
				, GetFence()
				, m_LeafSemaphores
				, m_LeafStageFlags);
		m_LeafSemaphores.clear();
		m_LeafStageFlags.clear();
	}
	void FrameBoundResourcePool::AddLeafSempahores(vk::ArrayProxy<vk::Semaphore> semaphores)
	{
		CA_ASSERT(m_LeafStageFlags.size() == m_LeafSemaphores.size(), "Leaf Semaphore And Stage Flag Sizes Are Different!");
		m_LeafSemaphores.reserve(m_LeafSemaphores.size() + semaphores.size());
		m_LeafStageFlags.reserve(m_LeafStageFlags.size() + semaphores.size());
		for (vk::Semaphore semaphore : semaphores)
		{
			m_LeafSemaphores.push_back(semaphore);
			m_LeafStageFlags.push_back(vk::PipelineStageFlagBits::eAllCommands);
		}
	}
}