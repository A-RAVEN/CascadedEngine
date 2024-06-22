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
	class QueueContext;
	class GPUMemoryResourceManager;
	class GPUResourceObjectManager;
	class GlobalResourceReleaseQueue;
#pragma endregion

	class VKAppSubObjectBase
	{
	public:
		VKAppSubObjectBase(CVulkanApplication& app);
		virtual ~VKAppSubObjectBase() {};
		VKAppSubObjectBase(VKAppSubObjectBase const&) = default;
		VKAppSubObjectBase& operator=(VKAppSubObjectBase const&) = default;
		VKAppSubObjectBase(VKAppSubObjectBase&&) = default;
		VKAppSubObjectBase& operator=(VKAppSubObjectBase&&) = default;
		CVulkanApplication& GetVulkanApplication() const;
		vk::Instance GetInstance() const;
		vk::Device GetDevice() const;
		vk::PhysicalDevice GetPhysicalDevice() const;
		GPUObjectManager& GetGPUObjectManager();
		GPUMemoryResourceManager& GetGlobalMemoryManager();
		GPUResourceObjectManager& GetGlobalResourceObjectManager();
		QueueContext& GetQueueContext();
	private:
		CVulkanApplication& m_OwningApplication;
	};

	class VKAppSubObjectBaseNoCopy
	{
	public:
		VKAppSubObjectBaseNoCopy() = default;
		VKAppSubObjectBaseNoCopy(CVulkanApplication& owner);
		VKAppSubObjectBaseNoCopy(VKAppSubObjectBaseNoCopy const& other) = delete;
		VKAppSubObjectBaseNoCopy& operator=(VKAppSubObjectBaseNoCopy const&) = delete;
		VKAppSubObjectBaseNoCopy(VKAppSubObjectBaseNoCopy&& other) = default;
		VKAppSubObjectBaseNoCopy& operator=(VKAppSubObjectBaseNoCopy&&) = default;
		virtual ~VKAppSubObjectBaseNoCopy() {};
		CVulkanApplication& GetVulkanApplication() const;
		vk::Instance GetInstance() const;
		vk::Device GetDevice() const;
		vk::PhysicalDevice GetPhysicalDevice() const;

		GPUObjectManager& GetGPUObjectManager();
		GPUMemoryResourceManager& GetGlobalMemoryManager();
		GPUResourceObjectManager& GetGlobalResourceObjectManager();
		GlobalResourceReleaseQueue& GetGlobalResourecReleasingQueue();
		QueueContext& GetQueueContext();
	private:
		CVulkanApplication* m_OwningApplication = nullptr;
	};
}