#pragma once
#include <WindowHandle.h>
#include <Utils/VulkanSubobjectBase.h>
#include <GPUGraph/VulkanResourceState.h>
#include <CASTL/CAVector.h>
#include <memory>

namespace cawindow
{
	class IWindow;
}

namespace graphics_backend
{
	class VulkanWindowHandle : public WindowHandle, public VulkanSubobjectBase
	{
	public:
		VulkanWindowHandle() = default;
		~VulkanWindowHandle();
		VulkanWindowHandle(VulkanWindowHandle&&) = delete;
		VulkanWindowHandle& operator=(VulkanWindowHandle&&) = delete;

		void Init(castl::shared_ptr<cawindow::IWindow> window);
		virtual void Release() override;

		// WindowHandle interface
		virtual uint2 GetSizeSafe() const override;
		virtual GPUTextureDescriptor const& GetBackbufferDescriptor() const override { return m_BackbufferDescriptor; }

		// Vulkan-specific methods
		vk::SurfaceKHR GetSurface() const { return m_Surface; }
		vk::SwapchainKHR GetSwapchain() const { return m_Swapchain; }
		vk::Format GetSwapchainFormat() const { return m_Format; }
		vk::Extent2D GetSwapchainExtent() const { return m_Extent; }

		// Swapchain operations
		uint32_t AcquireNextImage(vk::Semaphore semaphore, vk::Fence fence = {});
		void Present(vk::Queue queue, vk::Semaphore waitSemaphore);

		// Get current swapchain image
		vk::Image GetCurrentImage() const;
		vk::ImageView GetCurrentImageView() const;
		uint32_t GetCurrentImageIndex() const { return m_CurrentImageIndex; }

		// Check if swapchain needs recreation
		bool NeedsRecreation() const { return m_SwapchainOutdated; }
		void RecreateSwapchain();

		// Check if window is valid
		bool IsValid() const { return m_Surface && m_Swapchain; }

		// Per-swapchain-image backbuffer resource state (aligned with D3D12 WindowContext)
		void ApplyCurrentBackBufferResourceState(VulkanResourceState const& state);
		VulkanResourceState const& GetCurrentBackBufferResourceState() const;

	private:
		void CreateSurface();
		void CreateSwapchain();
		void CreateImageViews();
		void CleanupSwapchain();

		vk::SurfaceFormatKHR ChooseSurfaceFormat(castl::vector<vk::SurfaceFormatKHR> const& availableFormats);
		vk::PresentModeKHR ChoosePresentMode(castl::vector<vk::PresentModeKHR> const& availableModes);
		vk::Extent2D ChooseSwapchainExtent(vk::SurfaceCapabilitiesKHR const& capabilities);

		castl::shared_ptr<cawindow::IWindow> m_Window;
		vk::SurfaceKHR m_Surface;
		vk::SwapchainKHR m_Swapchain;

		vk::Format m_Format = vk::Format::eB8G8R8A8Unorm;
		vk::ColorSpaceKHR m_ColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
		vk::Extent2D m_Extent;

		castl::vector<vk::Image> m_SwapchainImages;
		castl::vector<vk::ImageView> m_SwapchainImageViews;
		uint32_t m_CurrentImageIndex = 0;

		GPUTextureDescriptor m_BackbufferDescriptor{};
		bool m_SwapchainOutdated = false;
		bool m_Released = false;

		castl::vector<VulkanResourceState> m_BackBufferResourceStates;
	};
}
