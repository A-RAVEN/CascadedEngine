#pragma once
#include  <vulkan/vulkan.hpp>
#include "RenderBackendSettings.h"
#include "VulkanApplicationSubobjectBase.h"
#include <ExternalLib/VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <deque>
#include <map>
#include "CVulkanBufferObject.h"
#include "Containers.h"
#include "VulkanImageObject.h"
#include <RenderInterface/header/GPUTexture.h>

namespace graphics_backend
{
	enum class EMemoryType
	{
		//gpu可见
		GPU,
		//cpu可见，随机读写
		CPU_Random_Access,
		//cpu可见，顺序读写
		CPU_Sequential_Access,
	};

	enum class EMemoryLifetime
	{
		Persistent,
		FrameBound,
	};

	class IVulkanBufferPool
	{
	public:
		virtual VulkanBufferHandle AllocateBuffer(EMemoryType memoryType, size_t bufferSize, vk::BufferUsageFlags bufferUsage) = 0;
	};

	class IVulkanImagePool
	{
	public:
		virtual VulkanImageObject AllocateImage(GPUTextureDescriptor const& textureDescriptor) = 0;
		virtual void ReleaseImage(VulkanImageObject& releasingImage) = 0;
	};

	class CFrameBoundMemoryPool : public VKAppSubObjectBaseNoCopy, public IVulkanBufferPool
	{
	public:
		CFrameBoundMemoryPool(uint32_t pool_id, CVulkanApplication& owner);
		virtual VulkanBufferHandle AllocateBuffer(EMemoryType memoryType, size_t bufferSize, vk::BufferUsageFlags bufferUsage) override;
		void ReleaseAllBuffers();
		void Initialize();
		void Release() override;
	private:
		std::mutex m_Mutex;
		VmaAllocator m_BufferAllocator = nullptr;
		std::deque<std::tuple<vk::Buffer, VmaAllocation>> m_ActiveBuffers;
		uint32_t m_PoolId;
	};


	class CGlobalMemoryPool : public VKAppSubObjectBaseNoCopy, public IVulkanBufferPool
	{
	public:
		CGlobalMemoryPool(CVulkanApplication& owner);
		VulkanBufferHandle AllocateBuffer(EMemoryType memoryType, size_t bufferSize, vk::BufferUsageFlags bufferUsage);
		void ReleaseBuffer(CVulkanBufferObject  const& returnBuffer);
		void ReleaseResourcesBeforeFrame(FrameType frame);
		void Initialize();
		void Release() override;

		VulkanImageObject AllocateImage(GPUTextureDescriptor const& textureDescriptor, EMemoryType memoryType);
	private:
		VmaAllocator m_GlobalAllocator = nullptr;
		std::mutex m_Mutex;
		std::deque<std::tuple<vk::Buffer, VmaAllocation, FrameType>> m_PendingReleasingBuffers;
		std::map<vk::Buffer, VmaAllocation> m_ActiveBuffers;
		TFrameboundReleaser<CVulkanBufferObject> m_BufferFrameboundReleaser;

		void ScheduleReleaseImage(VulkanImageObject_Internal& releasingImage);
		void ReleaseImage_Internal(std::deque<VulkanImageObject_Internal> const& releasingObjects);
		TFrameboundReleaser<VulkanImageObject_Internal> m_ImageFrameboundReleaser;
	};

	class CVulkanMemoryManager : public VKAppSubObjectBaseNoCopy
	{
	public:
		CVulkanMemoryManager(CVulkanApplication& owner);
		VulkanBufferHandle AllocateBuffer(EMemoryType memoryType, EMemoryLifetime lifetime, size_t bufferSize, vk::BufferUsageFlags bufferUsage);
		VulkanBufferHandle AllocateFrameBoundTransferStagingBuffer(size_t bufferSize);
		VulkanImageObject AllocateImage(GPUTextureDescriptor const& textureDescriptor, EMemoryType memoryType, EMemoryLifetime lifetime);
		void ReleaseCurrentFrameResource(FrameType releaseFrame, TIndex releasePoolIndex);
		void Initialize();
		void Release() override;
	private:
		CGlobalMemoryPool m_GlobalMemoryPool;
		std::deque<CFrameBoundMemoryPool> m_FrameBoundPool;
	};
}
