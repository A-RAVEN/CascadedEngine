#pragma once
#include <VulkanApplicationSubobjectBase.h>
#include <GPUResources/GPUResourceInternal.h>
namespace graphics_backend
{
	class GlobalResourceReleaseQueue : public VKAppSubObjectBaseNoCopy
	{
	public:
		GlobalResourceReleaseQueue(CVulkanApplication& app);
		void ReleaseGlobalResources();
		void AppendBuffersToReleaseQueue(vk::ArrayProxyNoTemporaries<VKBufferObject> const& bufferList);
		void AppendImagesToReleaseQueue(vk::ArrayProxyNoTemporaries<VKImageObject> const& bufferList);
	private:
		castl::vector<VKBufferObject> m_PendingBuffers;
		castl::vector<VKImageObject> m_PendingImages;
	};
}