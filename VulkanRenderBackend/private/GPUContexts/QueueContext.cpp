#include "QueueContext.h"

namespace graphics_backend
{
	QueueContext::QueueContext(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}
	void QueueContext::SubmitCurrentFrameGraphics(castl::vector<vk::CommandBuffer> const& commandbufferList, vk::ArrayProxyNoTemporaries<const vk::Semaphore> waitSemaphores, vk::ArrayProxyNoTemporaries<const vk::PipelineStageFlags> waitStages, vk::ArrayProxyNoTemporaries<const vk::Semaphore> signalSemaphores)
	{
	}
}