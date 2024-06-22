#pragma once
#include <VulkanApplicationSubobjectBase.h>
#include <GPUResources/GPUResourceInternal.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAArrayRef.h>
#include <WindowContext.h>
namespace graphics_backend
{
	class GlobalResourceReleaseQueue : public VKAppSubObjectBaseNoCopy
	{
	public:
		GlobalResourceReleaseQueue(CVulkanApplication& app);
		GlobalResourceReleaseQueue(GlobalResourceReleaseQueue&& other) noexcept;
		void ReleaseGlobalResources();
		void AddBuffers(castl::array_ref<VKBufferObject> const& bufferList);
		void AddImages(castl::array_ref<VKImageObject> const& bufferList);
		void AddSwapchains(castl::array_ref<SwapchainContext> const& swapchains);
		void Load(GlobalResourceReleaseQueue& other);
	private:
		castl::mutex m_Mutex;
		castl::vector<VKBufferObject> m_PendingBuffers;
		castl::vector<VKImageObject> m_PendingImages;
		castl::vector<SwapchainContext> m_PendingSwapchains;
	};
}