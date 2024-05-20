#include "ResourceReleaseQueue.h"

namespace graphics_backend
{
	GlobalResourceReleaseQueue::GlobalResourceReleaseQueue(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}
	void GlobalResourceReleaseQueue::ReleaseGlobalResources()
	{
	}
	void GlobalResourceReleaseQueue::AppendBuffersToReleaseQueue(vk::ArrayProxyNoTemporaries<VKBufferObject> const& bufferList)
	{
	}
	void GlobalResourceReleaseQueue::AppendImagesToReleaseQueue(vk::ArrayProxyNoTemporaries<VKImageObject> const& bufferList)
	{
	}
}