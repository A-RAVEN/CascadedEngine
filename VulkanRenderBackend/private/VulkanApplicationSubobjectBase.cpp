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
	CFrameCountContext const& VKAppSubObjectBase::GetFrameCountContext() const
	{
		return m_OwningApplication.GetSubmitCounterContext();
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
	CVulkanThreadContext* VKAppSubObjectBase::GetThreadContext(uint32_t threadIndex)
	{
		return m_OwningApplication.GetThreadContext(threadIndex);
	}
	GPUObjectManager& VKAppSubObjectBase::GetGPUObjectManager()
	{
		return m_OwningApplication.GetGPUObjectManager();
	}
	CVulkanMemoryManager& VKAppSubObjectBase::GetMemoryManager() const
	{
		return m_OwningApplication.GetMemoryManager();
	}

	VKAppSubObjectBaseNoCopy::VKAppSubObjectBaseNoCopy(CVulkanApplication& owner) :
		m_OwningApplication(owner)
	{
	}
	CVulkanApplication& VKAppSubObjectBaseNoCopy::GetVulkanApplication() const
	{
		return m_OwningApplication;
	}
	CFrameCountContext const& VKAppSubObjectBaseNoCopy::GetFrameCountContext() const
	{
		return m_OwningApplication.GetSubmitCounterContext();
	}
	vk::Instance VKAppSubObjectBaseNoCopy::GetInstance() const
	{
		return m_OwningApplication.GetInstance();
	}
	vk::Device VKAppSubObjectBaseNoCopy::GetDevice() const
	{
		return m_OwningApplication.GetDevice();
	}
	vk::PhysicalDevice VKAppSubObjectBaseNoCopy::GetPhysicalDevice() const
	{
		return m_OwningApplication.GetPhysicalDevice();
	}
	CVulkanThreadContext* VKAppSubObjectBaseNoCopy::GetThreadContext(uint32_t threadIndex)
	{
		return m_OwningApplication.GetThreadContext(threadIndex);
	}
	GPUObjectManager& VKAppSubObjectBaseNoCopy::GetGPUObjectManager()
	{
		return m_OwningApplication.GetGPUObjectManager();
	}
	CVulkanMemoryManager& VKAppSubObjectBaseNoCopy::GetMemoryManager() const
	{
		return m_OwningApplication.GetMemoryManager();
	}
}
