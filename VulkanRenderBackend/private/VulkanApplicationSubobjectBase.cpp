#include "pch.h"
#include "VulkanApplication.h"
#include "VulkanApplicationSubobjectBase.h"

namespace graphics_backend
{
	ApplicationSubobjectBase::~ApplicationSubobjectBase()
	{
		m_OwningApplication = nullptr;
	}

	void ApplicationSubobjectBase::Initialize(CVulkanApplication const* owningApplication)
	{
		m_OwningApplication = owningApplication;
		assert(m_OwningApplication != nullptr);
		Initialize_Internal(owningApplication);
	}

	void ApplicationSubobjectBase::Release()
	{
		Release_Internal();
		m_OwningApplication = nullptr;
	}
	CVulkanApplication const* ApplicationSubobjectBase::GetVulkanApplication() const
	{
		return m_OwningApplication;
	}

	CFrameCountContext const& ApplicationSubobjectBase::GetFrameCountContext() const
	{
		return m_OwningApplication->GetSubmitCounterContext();
	}

	vk::Instance ApplicationSubobjectBase::GetInstance() const
	{
		if (m_OwningApplication == nullptr)
			return vk::Instance{nullptr};
		return m_OwningApplication->GetInstance();
	}
	vk::Device ApplicationSubobjectBase::GetDevice() const
	{
		if (m_OwningApplication == nullptr)
			return nullptr;
		return m_OwningApplication->GetDevice();
	}
	vk::PhysicalDevice ApplicationSubobjectBase::GetPhysicalDevice() const
	{
		if (m_OwningApplication == nullptr)
			return nullptr;
		return m_OwningApplication->GetPhysicalDevice();
	}
	CVulkanThreadContext* ApplicationSubobjectBase::GetThreadContext(uint32_t threadIndex)
	{
		if (m_OwningApplication == nullptr)
			return nullptr;
		return m_OwningApplication->GetThreadContext(threadIndex);
	}

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
