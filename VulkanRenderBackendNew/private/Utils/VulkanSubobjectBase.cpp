#include <Utils/VulkanSubobjectBase.h>
#include <RenderBackend_Vulkan.h>

namespace graphics_backend
{
	RenderBackend_Vulkan* VulkanSubobjectBase::GetApp() const {
		return pApp;
	}
	vk::Instance const& VulkanSubobjectBase::GetInstance() const
	{
		return pApp->GetVulkanInstance();
	}
	vk::Device const& VulkanSubobjectBase::GetDevice() const 
	{
		return pApp->GetVulkanDevice();

	}
	vk::PhysicalDevice const& VulkanSubobjectBase::GetPhysicalDevice() const 
	{
		return pApp->GetVulkanPhysicalDevice();
	}

	QueueContext const& VulkanSubobjectBase::GetQueueContext() const
	{
		return pApp->GetQueueContext();
	}

}