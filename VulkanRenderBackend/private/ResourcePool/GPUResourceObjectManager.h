#pragma once
#include <GPUTexture.h>
#include <GPUBuffer.h>
#include <VulkanIncludes.h>
#include <CASTL/CASet.h>
#include <CASTL/CAMutex.h>
#include <VMA.h>
#include <VulkanApplicationSubobjectBase.h>

namespace graphics_backend
{
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
	private:
		castl::mutex m_Mutex;
		castl::set<vk::Image> m_ActivateImages;
		castl::set<vk::Buffer> m_ActiveBuffers;
	};
}
