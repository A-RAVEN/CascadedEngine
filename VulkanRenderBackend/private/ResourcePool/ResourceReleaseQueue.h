#pragma once
#include <VulkanApplicationSubobjectBase.h>
#include <GPUResources/GPUResourceInternal.h>
#include <CASTL/CAMutex.h>
namespace graphics_backend
{
	class GlobalResourceReleaseQueue : public VKAppSubObjectBaseNoCopy
	{
	public:
		GlobalResourceReleaseQueue(CVulkanApplication& app);
		GlobalResourceReleaseQueue(GlobalResourceReleaseQueue&& other) noexcept;
		void ReleaseGlobalResources();
		void AddBuffers(vk::ArrayProxyNoTemporaries<VKBufferObject> const& bufferList);
		void AddImages(vk::ArrayProxyNoTemporaries<VKImageObject> const& bufferList);
		void Load(GlobalResourceReleaseQueue& other);
	private:
		castl::mutex m_Mutex;
		castl::vector<VKBufferObject> m_PendingBuffers;
		castl::vector<VKImageObject> m_PendingImages;
	};
}