#pragma once
#include <CRenderBackend.h>
#include <ThreadManager.h>
#include <ShaderBindingBuilder.h>
#include <CAWindow/WindowSystem.h>
#include "D3D12Includes.h"
#include "WindowContext.h"

namespace graphics_backend
{
	class RenderBackend_D3D12 : public CRenderBackend
	{
	public:
		void Initialize(catimer::TimerSystem* timer, castl::string const& appName, castl::string const& engineName) override;
		void Release() override {}
		castl::shared_ptr<WindowHandle> GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window) override;
		bool AnyWindowRunning() override;
		virtual void ScheduleGPUFrame(TaskScheduler* scheduler, GPUFrame const& gpuFrame) override {}
		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(GPUBufferDescriptor const& descriptor) override { return nullptr; }
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) override { return nullptr; }
		ComPtr<IDXGIFactory4> GetFactory() const
		{
			return m_Factory;
		}
		ComPtr<ID3D12Device> GetDevice() const
		{
			return m_Device;
		}
		ComPtr<ID3D12CommandQueue> GetPresentQueue() const
		{
			return m_CommandQueue;
		}
	private:
		ComPtr<IDXGIFactory4> m_Factory;
		ComPtr<ID3D12Device> m_Device;
		ComPtr<ID3D12CommandQueue> m_CommandQueue;
		castl::unordered_map<castl::shared_ptr<cawindow::IWindow>, castl::shared_ptr<WindowContext>> m_WindowContexts;
	};
}
