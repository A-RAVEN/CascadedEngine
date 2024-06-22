#include "pch.h"
#include "VulkanApplication.h"
#include "VulkanApplicationSubobjectBase.h"

namespace graphics_backend
{
	VKAppSubObjectBase::VKAppSubObjectBase(CVulkanApplication& owner) :
		m_OwningApplication(owner)
	{
	}
	CVulkanApplication& VKAppSubObjectBase::GetVulkanApplication() const
	{
		return m_OwningApplication;
	}
	vk::Instance VKAppSubObjectBase::GetInstance() const
	{
		return m_OwningApplication.GetInstance();
	}
	vk::Device VKAppSubObjectBase::GetDevice() const
	{
		return m_OwningApplication.GetDevice();
	}
	vk::PhysicalDevice VKAppSubObjectBase::GetPhysicalDevice() const
	{
		return m_OwningApplication.GetPhysicalDevice();
	}
	GPUObjectManager& VKAppSubObjectBase::GetGPUObjectManager()
	{
		return m_OwningApplication.GetGPUObjectManager();
	}
	GPUMemoryResourceManager& VKAppSubObjectBase::GetGlobalMemoryManager()
	{
		return m_OwningApplication.GetGlobalMemoryManager();
	}
	GPUResourceObjectManager& VKAppSubObjectBase::GetGlobalResourceObjectManager()
	{
		return m_OwningApplication.GetGlobalResourceObjectManager();
	}
	QueueContext& VKAppSubObjectBase::GetQueueContext()
	{
		return m_OwningApplication.GetQueueContext();
	}

	VKAppSubObjectBaseNoCopy::VKAppSubObjectBaseNoCopy(CVulkanApplication& owner) :
		m_OwningApplication(&owner)
	{
	}
	CVulkanApplication& VKAppSubObjectBaseNoCopy::GetVulkanApplication() const
	{
		return *m_OwningApplication;
	}
	vk::Instance VKAppSubObjectBaseNoCopy::GetInstance() const
	{
		return m_OwningApplication->GetInstance();
	}
	vk::Device VKAppSubObjectBaseNoCopy::GetDevice() const
	{
		return m_OwningApplication->GetDevice();
	}
	vk::PhysicalDevice VKAppSubObjectBaseNoCopy::GetPhysicalDevice() const
	{
		return m_OwningApplication->GetPhysicalDevice();
	}
	GPUObjectManager& VKAppSubObjectBaseNoCopy::GetGPUObjectManager()
	{
		return m_OwningApplication->GetGPUObjectManager();
	}
	GPUMemoryResourceManager& VKAppSubObjectBaseNoCopy::GetGlobalMemoryManager()
	{
		return m_OwningApplication->GetGlobalMemoryManager();
	}
	GPUResourceObjectManager& VKAppSubObjectBaseNoCopy::GetGlobalResourceObjectManager()
	{
		return m_OwningApplication->GetGlobalResourceObjectManager();
	}
	GlobalResourceReleaseQueue& VKAppSubObjectBaseNoCopy::GetGlobalResourecReleasingQueue()
	{
		return m_OwningApplication->GetGlobalResourecReleasingQueue();
	}
	QueueContext& VKAppSubObjectBaseNoCopy::GetQueueContext()
	{
		return m_OwningApplication->GetQueueContext();
	}
}
