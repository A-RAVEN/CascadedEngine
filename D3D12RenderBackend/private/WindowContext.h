#pragma once
#include "D3D12Includes.h"
#include <WindowHandle.h>
#include <CAWindow/WindowSystem.h>

namespace graphics_backend
{
	class RenderBackend_D3D12;
	class WindowContext : public WindowHandle
	{
	public:
		WindowContext(RenderBackend_D3D12* app, castl::shared_ptr<cawindow::IWindow> windowHandle);
		virtual uint2 GetSizeSafe() const override;
		virtual GPUTextureDescriptor const& GetBackbufferDescriptor() const override;
	private:
		RenderBackend_D3D12* p_App;
		castl::shared_ptr<cawindow::IWindow> m_WindowHandle;
		ComPtr<IDXGISwapChain4> m_Swapchain;
		UINT m_FrameIndex;
	};
}