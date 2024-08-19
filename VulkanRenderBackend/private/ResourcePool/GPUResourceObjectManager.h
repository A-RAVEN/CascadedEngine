#pragma once
#include <GPUTexture.h>
#include <GPUBuffer.h>
#include <VulkanIncludes.h>
#include <CASTL/CASet.h>
#include <CASTL/CAMutex.h>
#include <VMA.h>
#include <VulkanApplicationSubobjectBase.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAMap.h>

namespace graphics_backend
{
	struct ActiveImageObjects
	{
		castl::map<GPUTextureView, vk::ImageView> views;
	};

	class GPUResourceObjectManager : public VKAppSubObjectBaseNoCopy
	{
	public:
		GPUResourceObjectManager(CVulkanApplication& app);
		GPUResourceObjectManager(GPUResourceObjectManager&& other) noexcept;
		void Initialize() {};
		void Release();
		void DestroyImage(vk::Image image);
		void DestroyBuffer(vk::Buffer buffer);
		void DestroyAll();
		vk::Image CreateImage(GPUTextureDescriptor const& desc);
		vk::Buffer CreateBuffer(GPUBufferDescriptor const& desc);

		vk::ImageView EnsureImageView(vk::Image image
			, GPUTextureDescriptor const& desc
			, GPUTextureView viewDesc);
	private:
		castl::mutex m_Mutex;
		castl::map<vk::Image, ActiveImageObjects> m_ActivateImages;
		castl::set<vk::Buffer> m_ActiveBuffers;
	};

	class SemaphorePool : public VKAppSubObjectBaseNoCopy
	{
	public:
		SemaphorePool(CVulkanApplication& app);
		void Release();
		void Reset();
		vk::Semaphore AllocSemaphore();
	private:
		castl::vector<vk::Semaphore> m_Semaphores;
		uint32_t m_SemaphoreIndex = 0;
	};
}
