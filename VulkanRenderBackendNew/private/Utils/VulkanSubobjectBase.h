#pragma once
#include <Utils/VulkanIncludes.h>

namespace graphics_backend
{
	class RenderBackend_Vulkan;
	class QueueContext;
	class VulkanSubobjectBase
	{
	public:
		VulkanSubobjectBase() = default;
		VulkanSubobjectBase(VulkanSubobjectBase&& other) = default;
		VulkanSubobjectBase(VulkanSubobjectBase const& other) = delete;
		VulkanSubobjectBase(VulkanSubobjectBase& other) = delete;

		VulkanSubobjectBase& operator=(VulkanSubobjectBase&& other) = default;

		virtual void Release() {};
		RenderBackend_Vulkan* GetApp() const;
		vk::Instance const& GetInstance() const;
		vk::Device const& GetDevice() const;
		vk::PhysicalDevice const& GetPhysicalDevice() const;
		QueueContext const& GetQueueContext() const;

	private:
		void SetApp(RenderBackend_Vulkan* app) {
			pApp = app;
		}
		RenderBackend_Vulkan* pApp = nullptr;
		friend class RenderBackend_Vulkan;
	};
}