#pragma once
#include <CRenderBackend.h>
#include <ThreadManager.h>
#include <ShaderBindingBuilder.h>
#include <CAWindow/WindowSystem.h>
#include <ResourceManagment/MemoryManager.h>
#include "D3D12Includes.h"
#include "WindowContext.h"

namespace graphics_backend
{
	class RenderBackend_D3D12 : public CRenderBackend
	{
	public:
		RenderBackend_D3D12();
		void Initialize(catimer::TimerSystem* timer, castl::string const& appName, castl::string const& engineName) override;
		void Release() override;
		castl::shared_ptr<WindowHandle> GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window) override;
		bool AnyWindowRunning() override;
		virtual void ScheduleGPUFrame(TaskScheduler* scheduler, GPUFrame const& gpuFrame) override {}
		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(GPUBufferDescriptor const& descriptor) override;
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) override;
		ComPtr<IDXGIFactory4> GetFactory() const
		{
			return m_Factory;
		}
		ComPtr<ID3D12Device> GetDevice() const
		{
			return m_Device;
		}
		ComPtr<IDXGIAdapter1> GetAdapter() const
		{
			return m_Adapter;
		}
		ComPtr<ID3D12CommandQueue> GetPresentQueue() const
		{
			return m_CommandQueue;
		}

		//template<typename T, typename...TArgs>
		//T SubObject(TArgs&...Args) {
		//	static_assert(castl::is_constructible_v<T, RenderBackend_D3D12&> || castl::is_constructible_v<T, RenderBackend_D3D12&, TArgs...>
		//		, "Type T Not Compatible To Vulkan SubObject");
		//	if constexpr (castl::is_constructible_v<T, RenderBackend_D3D12&, TArgs...>)
		//	{
		//		castl::shared_ptr<T> newSubObject = castl::shared_ptr<T>{ new T(*this, castl::forward<TArgs>(Args)...), SubObjectDefaultDeleter<T>{} };
		//		if constexpr (has_initialize<T>)
		//		{
		//			newSubObject->Initialize();
		//		}
		//		else if constexpr (has_create<T>)
		//		{
		//			newSubObject->Create();
		//		}
		//		return newSubObject;
		//	}
		//	else
		//	{
		//		castl::shared_ptr<T> newSubObject = castl::shared_ptr<T>{ new T(*this), SubObjectDefaultDeleter<T>{} };
		//		if constexpr (has_initialize<T, TArgs...>)
		//		{
		//			newSubObject->Initialize(castl::forward<TArgs>(Args)...);
		//		}
		//		else if constexpr (has_create<T, TArgs...>)
		//		{
		//			newSubObject->Create(castl::forward<TArgs>(Args)...);
		//		}
		//		return newSubObject;
		//	}
		//};

	private:
		MemoryManager m_MemoryManager;
		ComPtr<IDXGIFactory4> m_Factory;
		ComPtr<ID3D12Device> m_Device;
		ComPtr<IDXGIAdapter1> m_Adapter;
		ComPtr<ID3D12CommandQueue> m_CommandQueue;
		castl::unordered_map<castl::shared_ptr<cawindow::IWindow>, castl::shared_ptr<WindowContext>> m_WindowContexts;
	};
}
