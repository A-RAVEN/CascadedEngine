#pragma once
#include "D3D12Includes.h"
#include <WindowHandle.h>
#include <CAWindow/WindowSystem.h>
#include <ResourceManagment/GPUResourceStates.h>
#include <Utils/D3D12SubobjectBase.h>
#include <semaphore>

namespace graphics_backend
{
	class RenderBackend_D3D12;
	class WindowContext : public WindowHandle, D3D12SubobjectBase
	{
	public:
		WindowContext(RenderBackend_D3D12* app, castl::shared_ptr<cawindow::IWindow> windowHandle);
		virtual uint2 GetSizeSafe() const override;
		virtual GPUTextureDescriptor const& GetBackbufferDescriptor() const override;
		ComPtr<ID3D12Resource> const& GetCurrentBackBufferResource() const;
		DescriptorAllocation EnsureCurrentBackBufferRTV();
		ResourceState const& GetCurrentBackBufferResourceState() const;
		void ApplyCurrentBackBufferResourceState(ResourceState const& resourceState);
		void Present();
		void Aquire()
		{
			m_Semaphore.acquire();
		}
		void Release()
		{
			m_Semaphore.release();
		}
	private:
		struct BackbufferData
		{
			ComPtr<ID3D12Resource> backbufferResource;
			TextureResourceViews resourceViews;
			ResourceState resourceState;
		};

		mutable castl::binary_semaphore m_Semaphore;
		mutable castl::shared_mutex m_Mutex;
		GPUTextureDescriptor m_BackBufferDesc;
		castl::shared_ptr<cawindow::IWindow> m_WindowHandle;
		ComPtr<IDXGISwapChain4> m_Swapchain;
		castl::vector<BackbufferData> m_BackBuffers;
		UINT m_BackBufferIndex;
	};
}