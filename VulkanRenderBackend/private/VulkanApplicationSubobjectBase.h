#pragma once
#include "VulkanIncludes.h"

namespace graphics_backend
{
#pragma region Forward Declaration
	class CVulkanApplication;
	class CVulkanThreadContext;
	class CFrameCountContext;
	class CVulkanMemoryManager;
	class GPUObjectManager;
#pragma endregion

	class ApplicationSubobjectBase
	{
	public:
		ApplicationSubobjectBase() = default;
		ApplicationSubobjectBase(ApplicationSubobjectBase const& other) = delete;
		ApplicationSubobjectBase& operator=(ApplicationSubobjectBase const&) = delete;
		ApplicationSubobjectBase(ApplicationSubobjectBase&& other) = default;
		ApplicationSubobjectBase& operator=(ApplicationSubobjectBase&&) = default;
		virtual void Initialize(CVulkanApplication const* owningApplication);
		virtual void Release();
		CVulkanApplication const* GetVulkanApplication() const;
		CFrameCountContext const& GetFrameCountContext() const;
		vk::Instance GetInstance() const;
		vk::Device GetDevice() const;
		vk::PhysicalDevice GetPhysicalDevice() const;
		CVulkanThreadContext* GetThreadContext(uint32_t threadIndex);
		virtual ~ApplicationSubobjectBase();
	protected:
		virtual void Initialize_Internal(CVulkanApplication const* owningApplication) = 0;
		virtual void Release_Internal() = 0;
		CVulkanApplication const* m_OwningApplication = nullptr;
	};

	struct ApplicationSubobjectBase_Deleter {
		void operator()(ApplicationSubobjectBase* deleteObject) {
			deleteObject->Release();
			delete deleteObject;
		}
	};


	class VKAppSubObjectBase
	{
	public:
		VKAppSubObjectBase(CVulkanApplication& app);
		virtual ~VKAppSubObjectBase() {};
		VKAppSubObjectBase(VKAppSubObjectBase const&) = default;
		VKAppSubObjectBase& operator=(VKAppSubObjectBase const&) = default;
		VKAppSubObjectBase(VKAppSubObjectBase&&) = default;
		VKAppSubObjectBase& operator=(VKAppSubObjectBase&&) = default;
		virtual void Release() {};
		CVulkanApplication& GetVulkanApplication() const;
		CFrameCountContext const& GetFrameCountContext() const;
		vk::Instance GetInstance() const;
		vk::Device GetDevice() const;
		vk::PhysicalDevice GetPhysicalDevice() const;
		CVulkanThreadContext* GetThreadContext(uint32_t threadIndex);
		GPUObjectManager& GetGPUObjectManager();
		CVulkanMemoryManager& GetMemoryManager() const;
	private:
		CVulkanApplication& m_OwningApplication;
	};

	class VKAppSubObjectBaseNoCopy
	{
	public:
		VKAppSubObjectBaseNoCopy() = delete;
		VKAppSubObjectBaseNoCopy(CVulkanApplication& owner);
		VKAppSubObjectBaseNoCopy(VKAppSubObjectBaseNoCopy const& other) = delete;
		VKAppSubObjectBaseNoCopy& operator=(VKAppSubObjectBaseNoCopy const&) = delete;
		VKAppSubObjectBaseNoCopy(VKAppSubObjectBaseNoCopy&& other) = default;
		VKAppSubObjectBaseNoCopy& operator=(VKAppSubObjectBaseNoCopy&&) = default;
		virtual ~VKAppSubObjectBaseNoCopy() {};
		virtual void Release() {};
		CVulkanApplication& GetVulkanApplication() const;
		CFrameCountContext const& GetFrameCountContext() const;
		vk::Instance GetInstance() const;
		vk::Device GetDevice() const;
		vk::PhysicalDevice GetPhysicalDevice() const;
		CVulkanThreadContext* GetThreadContext(uint32_t threadIndex);
		GPUObjectManager& GetGPUObjectManager();
		CVulkanMemoryManager &GetMemoryManager() const;
	private:
		CVulkanApplication& m_OwningApplication;
	};


}