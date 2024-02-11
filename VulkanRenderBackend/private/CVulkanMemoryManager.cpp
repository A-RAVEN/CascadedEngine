#include "pch.h"
#include <GPUTexture.h>
#include "CVulkanMemoryManager.h"
#include "VulkanApplication.h"
#include "VulkanImageObject.h"
#include "InterfaceTranslator.h"

namespace graphics_backend
{
	constexpr VmaAllocationCreateInfo EMemoryTypeToAllocationCreateInfo(EMemoryType memoryType)
	{
		VmaAllocationCreateInfo allocationCreateInfo{};
		switch (memoryType)
		{
		case EMemoryType::GPU:
		{
			allocationCreateInfo.flags = 0;
			allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		}
		break;
		case EMemoryType::CPU_Random_Access:
		{
			allocationCreateInfo.flags =
				VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
				| VMA_ALLOCATION_CREATE_MAPPED_BIT;
			allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		}
		break;
		case EMemoryType::CPU_Sequential_Access:
		{
			allocationCreateInfo.flags =
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
				| VMA_ALLOCATION_CREATE_MAPPED_BIT;
			allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		}
		break;
		}
		return allocationCreateInfo;
	}

	void AllocateBuffer_Common(VmaAllocator allocator, EMemoryType memoryType, size_t bufferSize, vk::BufferUsageFlags bufferUsage
		, vk::Buffer& outBuffer, VmaAllocation& outAllocation, VmaAllocationInfo& outAllocationInfo)
	{
		VkBufferCreateInfo bufferCreateInfo = vk::BufferCreateInfo(
			{}, bufferSize, bufferUsage, vk::SharingMode::eExclusive
		);
		VmaAllocationCreateInfo allocationCreateInfo = EMemoryTypeToAllocationCreateInfo(memoryType);
		VkBuffer vkbuffer_c;
		vmaCreateBuffer(
			allocator
			, &bufferCreateInfo
			, &allocationCreateInfo
			, &vkbuffer_c
			, &outAllocation
			, &outAllocationInfo);
		outBuffer = vkbuffer_c;
	}

	void AllocateImage_Common(VmaAllocator allocator, GPUTextureDescriptor const& desc, EMemoryType memoryType
		, vk::Image& outImage, VmaAllocation& outAllocation, VmaAllocationInfo& outAllocationInfo)
	{
		VulkanImageInfo imageInfo = ETextureTypeToVulkanImageInfo(desc.textureType);
		bool is3D = imageInfo.imageType == vk::ImageType::e3D;
		vk::ImageUsageFlags usages = ETextureAccessTypeToVulkanImageUsageFlags(desc.format, desc.accessType);
		VkImageCreateInfo imageCreateInfo = vk::ImageCreateInfo{imageInfo.createFlags
			, imageInfo.imageType
			, ETextureFormatToVkFotmat(desc.format)
			, vk::Extent3D{
				desc.width
				, desc.height
				, is3D ? desc.layers : 1
			}
			, desc.mipLevels
			, is3D ? 1 : desc.layers
			, vk::SampleCountFlagBits::e1
				, vk::ImageTiling::eOptimal
				, usages
		};
		VmaAllocationCreateInfo allocationCreateInfo = EMemoryTypeToAllocationCreateInfo(memoryType);
		VkImage vkImage_c;
		vmaCreateImage(
			allocator
			, &imageCreateInfo
			, &allocationCreateInfo
			, &vkImage_c
			, &outAllocation
			, &outAllocationInfo);
		outImage = vkImage_c;
	}

	CFrameBoundMemoryPool::CFrameBoundMemoryPool(uint32_t pool_id, CVulkanApplication& owner) :
		VKAppSubObjectBase(owner)
		, m_PoolId(pool_id)
	{
	}

	VulkanBufferHandle CFrameBoundMemoryPool::AllocateBuffer(
		EMemoryType memoryType
		, size_t bufferSize
		, vk::BufferUsageFlags bufferUsage)
	{
		vk::Buffer buffer{};
		VmaAllocation allocation{};
		VmaAllocationInfo allocationInfo{};
		AllocateBuffer_Common(m_BufferAllocator, memoryType, bufferSize, bufferUsage
			, buffer, allocation, allocationInfo);

		auto bufferObj = CVulkanBufferObject{ buffer, allocation, allocationInfo };
		auto handle = VulkanBufferHandle(castl::move(bufferObj), [this](CVulkanBufferObject& releasedObj)
			{
			});
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		m_ActiveBuffers.emplace_back(castl::make_tuple(buffer, allocation));
		return handle;
	}

	void CFrameBoundMemoryPool::ReleaseAllBuffers()
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		for (auto pending_release_buffer : m_ActiveBuffers)
		{
			vmaDestroyBuffer(m_BufferAllocator
				, castl::get<0>(pending_release_buffer)
				, castl::get<1>(pending_release_buffer));
		}
		m_ActiveBuffers.clear();
	}

	void CFrameBoundMemoryPool::Initialize()
	{
		VmaAllocatorCreateInfo vmaCreateInfo{};
		vmaCreateInfo.vulkanApiVersion = VULKAN_API_VERSION_IN_USE;
		vmaCreateInfo.physicalDevice = static_cast<VkPhysicalDevice>(GetPhysicalDevice());
		vmaCreateInfo.device = GetDevice();
		vmaCreateInfo.instance = GetInstance();
		vmaCreateAllocator(&vmaCreateInfo, &m_BufferAllocator);
	}

	void CFrameBoundMemoryPool::Release()
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		for (auto pending_release_buffer : m_ActiveBuffers)
		{
			vmaDestroyBuffer(m_BufferAllocator
				, castl::get<0>(pending_release_buffer)
				, castl::get<1>(pending_release_buffer));
		}
		m_ActiveBuffers.clear();
		vmaDestroyAllocator(m_BufferAllocator);
	}

	CGlobalMemoryPool::CGlobalMemoryPool(CVulkanApplication& owner) : VKAppSubObjectBaseNoCopy(owner)
		, m_ImageFrameboundReleaser([this](castl::deque<VulkanImageObject_Internal> const& releasingObjects)
			{
				ReleaseImage_Internal(releasingObjects);
			})
		, m_BufferFrameboundReleaser([this](castl::deque<CVulkanBufferObject> const& releasingBuffers)
			{
				for (CVulkanBufferObject const& releasingBuffer : releasingBuffers)
				{
					ReleaseBuffer(releasingBuffer);
				}
			})
	{
	}

	VulkanBufferHandle CGlobalMemoryPool::AllocateBuffer(
		EMemoryType memoryType
		, size_t bufferSize
		, vk::BufferUsageFlags bufferUsage)
	{

		vk::Buffer buffer{};
		VmaAllocation allocation{};
		VmaAllocationInfo allocationInfo{};
		AllocateBuffer_Common(m_GlobalAllocator, memoryType, bufferSize, bufferUsage
			, buffer, allocation, allocationInfo);
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		m_ActiveBuffers.insert(castl::make_pair(buffer, allocation));

		auto bufferObj = CVulkanBufferObject{ buffer, allocation, allocationInfo };
		auto handle = VulkanBufferHandle(castl::move(bufferObj), [this](CVulkanBufferObject& releasedObj)
			{
				FrameType releasingFrameID = GetFrameCountContext().GetCurrentFrameID();
				m_BufferFrameboundReleaser.ScheduleRelease(releasingFrameID
					, castl::move(releasedObj));
			});
		return handle;
	}

	void CGlobalMemoryPool::ReleaseBuffer(CVulkanBufferObject const& releasingBuffer)
	{
		vmaDestroyBuffer(m_GlobalAllocator
			, releasingBuffer.m_Buffer
			, releasingBuffer.m_BufferAllocation);
	}

	void CGlobalMemoryPool::ReleaseResourcesBeforeFrame(FrameType frame)
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		while((!m_PendingReleasingBuffers.empty())
			&& frame >= castl::get<2>(m_PendingReleasingBuffers.front()))
		{
			auto frontBuffer = m_PendingReleasingBuffers.front();
			m_PendingReleasingBuffers.pop_front();
			vmaDestroyBuffer(m_GlobalAllocator
				, castl::get<0>(frontBuffer)
				, castl::get<1>(frontBuffer));
		}
		m_ImageFrameboundReleaser.ReleaseFrame(frame);
	}

	void CGlobalMemoryPool::Initialize()
	{
		VmaAllocatorCreateInfo vmaCreateInfo{};
		vmaCreateInfo.vulkanApiVersion = VULKAN_API_VERSION_IN_USE;
		vmaCreateInfo.physicalDevice = static_cast<VkPhysicalDevice>(GetPhysicalDevice());
		vmaCreateInfo.device = GetDevice();
		vmaCreateInfo.instance = GetInstance();
		vmaCreateAllocator(&vmaCreateInfo, &m_GlobalAllocator);
	}

	void CGlobalMemoryPool::Release()
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		for(auto pending_release_buffer : m_PendingReleasingBuffers)
		{
			vmaDestroyBuffer(m_GlobalAllocator
				, castl::get<0>(pending_release_buffer)
				, castl::get<1>(pending_release_buffer));
		}
		m_PendingReleasingBuffers.clear();
		for (auto pending_release_buffer : m_ActiveBuffers)
		{
			vmaDestroyBuffer(m_GlobalAllocator
				, pending_release_buffer.first
				, pending_release_buffer.second);
		}
		m_ActiveBuffers.clear();
		vmaDestroyAllocator(m_GlobalAllocator);
		m_GlobalAllocator = nullptr;
		m_ImageFrameboundReleaser.ReleaseAll();
	}

	CVulkanMemoryManager::CVulkanMemoryManager(CVulkanApplication& owner) : 
		VKAppSubObjectBaseNoCopy(owner)
		, m_GlobalMemoryPool(owner)
	{
	}

	VulkanBufferHandle CVulkanMemoryManager::AllocateBuffer(EMemoryType memoryType, EMemoryLifetime lifetime,
	                                                         size_t bufferSize, vk::BufferUsageFlags bufferUsage)
	{
		switch(lifetime)
		{
		case EMemoryLifetime::FrameBound:
			{
				uint32_t poolIndex = GetFrameCountContext().GetCurrentFrameBufferIndex();
				return m_FrameBoundPool[poolIndex].AllocateBuffer(memoryType, bufferSize, bufferUsage);
			}
		case EMemoryLifetime::Persistent:
			return m_GlobalMemoryPool.AllocateBuffer(memoryType, bufferSize, bufferUsage);
		}
		return VulkanBufferHandle{};
	}

	VulkanBufferHandle CVulkanMemoryManager::AllocateFrameBoundTransferStagingBuffer(size_t bufferSize)
	{
		return AllocateBuffer(
			EMemoryType::CPU_Sequential_Access
			, EMemoryLifetime::FrameBound
			, bufferSize
			, vk::BufferUsageFlagBits::eTransferSrc);
	}

	VulkanImageObject CVulkanMemoryManager::AllocateImage(GPUTextureDescriptor const& textureDescriptor, EMemoryType memoryType, EMemoryLifetime lifetime)
	{
		return m_GlobalMemoryPool.AllocateImage(textureDescriptor, memoryType);
	}

	void CVulkanMemoryManager::ReleaseCurrentFrameResource(FrameType releaseFrame, TIndex releasePoolIndex)
	{
		//if (!GetFrameCountContext().AnyFrameFinished())
		//	return;
		//TIndex const releasingPoolIndex = GetFrameCountContext().GetReleasedResourcePoolIndex();
		//FrameType const releasingFrame = GetFrameCountContext().GetReleasedFrameID();
		m_GlobalMemoryPool.ReleaseResourcesBeforeFrame(releaseFrame);
		m_FrameBoundPool[releasePoolIndex].ReleaseAllBuffers();
	}

	void CVulkanMemoryManager::Initialize()
	{
		m_GlobalMemoryPool.Initialize();
		m_FrameBoundPool.clear();
		//m_FrameBoundPool.reserve(FRAMEBOUND_RESOURCE_POOL_SWAP_COUNT_PER_CONTEXT);
		for(uint32_t i = 0; i < FRAMEBOUND_RESOURCE_POOL_SWAP_COUNT_PER_CONTEXT; ++i)
		{
			m_FrameBoundPool.emplace_back(i, GetVulkanApplication());
			m_FrameBoundPool[i].Initialize();
		}
	}

	void CVulkanMemoryManager::Release()
	{
		m_GlobalMemoryPool.Release();
		for(auto& itrPool : m_FrameBoundPool)
		{
			itrPool.Release();
		}
		m_FrameBoundPool.clear();
	}


	VulkanImageObject CGlobalMemoryPool::AllocateImage(GPUTextureDescriptor const& textureDescriptor, EMemoryType memoryType)
	{
		vk::Image resultImage;
		VmaAllocation allocation;
		VmaAllocationInfo allocationInfo;
		AllocateImage_Common(m_GlobalAllocator, textureDescriptor, EMemoryType::GPU
			, resultImage, allocation, allocationInfo);

		VulkanImageObject_Internal imgObjectInternal(
			textureDescriptor
			, resultImage
			, allocation
			, allocationInfo);
		return VulkanImageObject(castl::move(imgObjectInternal)
			, [this](VulkanImageObject_Internal& releasingImage)
			{
				ScheduleReleaseImage(releasingImage);
			});
	}

	void CGlobalMemoryPool::ScheduleReleaseImage(VulkanImageObject_Internal& releasingImage)
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		FrameType releasingFrameID = GetFrameCountContext().GetCurrentFrameID();
		m_ImageFrameboundReleaser.ScheduleRelease(releasingFrameID
			, castl::move(releasingImage));
	}
	void CGlobalMemoryPool::ReleaseImage_Internal(castl::deque<VulkanImageObject_Internal> const& releasingObjects)
	{
		for (VulkanImageObject_Internal const& obj : releasingObjects)
		{
			vmaDestroyImage(m_GlobalAllocator
				, obj.GetImage()
				, obj.GetAllocation());
		}
	}
}
