#include "WindowContext.h"
#include "D3D12Debug.h"
#include "RenderBackend_D3D12.h"

namespace graphics_backend
{
	WindowContext::WindowContext(RenderBackend_D3D12* app, castl::shared_ptr<cawindow::IWindow> windowHandle)
        : D3D12SubobjectBase(app), m_Semaphore(1)
	{
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

        m_BackBufferDesc = GPUTextureDescriptor::Create(width, height
            , ETextureFormat::E_R8G8B8A8_UNORM, ETextureType::e2D, 1, 1, EMultiSampleCount::e1);

        ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(GetApp()->GetFactory()->CreateSwapChainForHwnd(
            GetApp()->GetPresentQueue().Get(),        // Swap chain needs the queue so that it can force a flush on it.
            winHandle,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain
        ));

        ThrowIfFailed(swapChain.As(&m_Swapchain));
        m_BackBufferIndex = m_Swapchain->GetCurrentBackBufferIndex();

        for (UINT32 bufferID = 0; bufferID < FrameCount; ++bufferID)
        {
            ComPtr<ID3D12Resource> backBuffer;
            swapChain->GetBuffer(bufferID, IID_PPV_ARGS(&backBuffer));
            m_BackBuffers.push_back({ backBuffer, TextureResourceViews{}, ResourceState::InitializedState()});
        }
	}

    uint2 WindowContext::GetSizeSafe() const
    {
        uint2 result;
        m_Swapchain->GetSourceSize(&result.x, &result.y);
        return result;
    }
    bool WindowContext::NeedResize()
    {
        int2 targetSize;
        m_WindowHandle->GetWindowSize(targetSize.x, targetSize.y);
        uint2 currentSize = GetSizeSafe();
        if (currentSize.x == targetSize.x && currentSize.y == targetSize.y)
            return false;
        return true;
    }

    void WindowContext::CheckResize()
    {
        UINT32 FrameCount = 3;
        int2 targetSize;
        m_WindowHandle->GetWindowSize(targetSize.x, targetSize.y);
		uint2 currentSize = GetSizeSafe();
        if (currentSize.x == targetSize.x && currentSize.y == targetSize.y)
            return;
        m_BackBufferDesc.width = targetSize.x;
        m_BackBufferDesc.height = targetSize.y;
        m_BackBuffers.clear();
        ThrowIfFailed(m_Swapchain->ResizeBuffers(FrameCount, targetSize.x, targetSize.y, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
        m_BackBufferIndex = m_Swapchain->GetCurrentBackBufferIndex();
        for (UINT32 bufferID = 0; bufferID < FrameCount; ++bufferID)
        {
            ComPtr<ID3D12Resource> backBuffer;
            m_Swapchain->GetBuffer(bufferID, IID_PPV_ARGS(&backBuffer));
            m_BackBuffers.push_back({ backBuffer, TextureResourceViews{}, ResourceState::InitializedState() });
        }
    }
    GPUTextureDescriptor const& WindowContext::GetBackbufferDescriptor() const
    {
        return  m_BackBufferDesc;
    }
    ComPtr<ID3D12Resource> const& WindowContext::GetCurrentBackBufferResource() const
    {
        return m_BackBuffers[m_BackBufferIndex].backbufferResource;
    }
    DescriptorAllocation WindowContext::EnsureCurrentBackBufferRTV()
    {
        auto& currentBuffer = m_BackBuffers[m_BackBufferIndex];
        return currentBuffer.resourceViews.EnsureRTV(GetApp(), GetApp()->GetCommonDescriptorAllocatorSet(), m_Mutex
            , currentBuffer.backbufferResource.Get(), m_BackBufferDesc
            , GPUTextureView::CreateDefaultForRenderTarget(m_BackBufferDesc.format));
    }
    ResourceState const& WindowContext::GetCurrentBackBufferResourceState() const
    {
        return m_BackBuffers[m_BackBufferIndex].resourceState;
    }
    void WindowContext::ApplyCurrentBackBufferResourceState(ResourceState const& resourceState)
    {
        m_BackBuffers[m_BackBufferIndex].resourceState = resourceState;
    }
    void WindowContext::Present()
    {
        m_Swapchain->Present(0, 0);
        m_BackBufferIndex = m_Swapchain->GetCurrentBackBufferIndex();
    }
}