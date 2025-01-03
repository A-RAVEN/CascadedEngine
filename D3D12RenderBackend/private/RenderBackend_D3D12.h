#pragma once
#include <Utils/TypeTraits.h>
#include <CRenderBackend.h>
#include <ThreadManager.h>
#include <ShaderBindingBuilder.h>
#include <CAWindow/WindowSystem.h>
#include <ResourceManagment/MemoryManager.h>
#include <CAResource/ResourceManagingSystem.h>
#include "D3D12Includes.h"
#include "WindowContext.h"

namespace graphics_backend
{
	class RenderBackend_D3D12 : public CRenderBackend
	{
	public:
		RenderBackend_D3D12();
		void Initialize(catimer::TimerSystem* timer
			, ca_io::IOManager* ioManager
			, resource_management::ResourceManagingSystem* resourceManager
			, castl::string const& appName
			, castl::string const& engineName) override;
		void Release() override;
		castl::shared_ptr<WindowHandle> GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window) override;
		bool AnyWindowRunning() override;
		virtual void ScheduleGPUFrame(TaskScheduler* scheduler, GPUFrame const& gpuFrame) override {}
		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(GPUBufferDescriptor const& descriptor) override;
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) override;

		virtual void RunTestCode() override;
		resource_management::ResourceManagingSystem* GetResourceManager() const
		{
			return p_ResourceManager;
		}
		ca_io::IOManager* GetIOManager() const
		{
			return p_IOManager;
		}
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

		template<typename T, typename...TArgs>
		static void InitObj(T* inoutObj, TArgs&...Args)
		{
			static_assert(CanInit<T, TArgs...>, "Type T Not Initializable");
			inoutObj->Init(castl::forward<TArgs>(Args)...);
		}

		template<typename T>
		struct SubObjectDefaultDeleter {
			void operator()(T* deleteObject)
			{
				if constexpr (CanRelease<T>)
				{
					deleteObject->Release();
				}
				delete deleteObject;
			}
		};

		template<typename T, typename...TArgs>
		castl::shared_ptr<T> NewSubObject_Shared(TArgs&...Args) {
			static_assert(castl::is_constructible_v<T, RenderBackend_D3D12*> || castl::is_constructible_v<T, RenderBackend_D3D12*, TArgs...>
				, "Type T Not Compatible To D3D12SubObject");
			if constexpr (castl::is_constructible_v<T, RenderBackend_D3D12*, TArgs...>)
			{
				castl::shared_ptr<T> newSubObject = castl::shared_ptr<T>{ new T(this, castl::forward<TArgs>(Args)...), SubObjectDefaultDeleter<T>{} };
				if constexpr (CanInit<T>)
				{
					newSubObject->Init();
				}
				return newSubObject;
			}
			else
			{
				castl::shared_ptr<T> newSubObject = castl::shared_ptr<T>{ new T(this), SubObjectDefaultDeleter<T>{} };
				if constexpr (CanInit<T, TArgs...>)
				{
					newSubObject->Init(castl::forward<TArgs>(Args)...);
				}
				return newSubObject;
			}
		};

	private:
		MemoryManager m_MemoryManager;
		ca_io::IOManager* p_IOManager;
		resource_management::ResourceManagingSystem* p_ResourceManager;
		ComPtr<IDXGIFactory4> m_Factory;
		ComPtr<ID3D12Device> m_Device;
		ComPtr<IDXGIAdapter1> m_Adapter;
		ComPtr<ID3D12CommandQueue> m_CommandQueue;
		castl::unordered_map<castl::shared_ptr<cawindow::IWindow>, castl::shared_ptr<WindowContext>> m_WindowContexts;
	};
}
