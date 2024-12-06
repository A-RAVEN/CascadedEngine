#include "WindowContext.h"
#include "D3D12Debug.h"
#include "RenderBackend_D3D12.h"

namespace graphics_backend
{
	WindowContext::WindowContext(RenderBackend_D3D12* app, castl::shared_ptr<cawindow::IWindow> windowHandle)
	{
		p_App = app;
		m_WindowHandle = windowHandle;

        UINT32 FrameCount = 3;
        HWND winHandle = *static_cast<HWND*>(m_WindowHandle->GetNativeWindowHandle());
        HINSTANCE winInstHandle = *static_cast<HINSTANCE*>(m_WindowHandle->GetWindowSystem()->GetSystemNativeHandle());
        int width, height;
        m_WindowHandle->GetWindowSize(width, height);
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = FrameCount;
        swapChainDesc.Width = width;
        swapChainDesc.Height = height;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(p_App->GetFactory()->CreateSwapChainForHwnd(
            p_App->GetPresentQueue().Get(),        // Swap chain needs the queue so that it can force a flush on it.
            winHandle,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain
        ));

        ThrowIfFailed(swapChain.As(&m_Swapchain));
        m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();
	}

    uint2 WindowContext::GetSizeSafe() const
    {
        uint2 result;
        m_Swapchain->GetSourceSize(&result.x, &result.y);
        return result;
    }
    GPUTextureDescriptor const& WindowContext::GetBackbufferDescriptor() const
    {
        return {};
    }
}