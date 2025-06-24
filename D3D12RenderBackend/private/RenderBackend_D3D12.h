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
//#include <GPUObjects/ShaderObject.h>
#include <ShaderLibrary/ShaderImporter_D12.h>
#include <ShaderLibrary/ShaderLibrary.h>
#include <DescriptorManagment/GPUDescriptorHeap.h>
#include <ResourceManagment/RootSignatureManager.h>
#include <GPUGraph/GPUPipelineInstance.h>
#include <GPUGraph/GPUComputePipelineInstance.h>
#include <CACore/CASharedList.h>

namespace graphics_backend
{
	class RenderBackend_D3D12 : public CRenderBackend
	{
	public:
		RenderBackend_D3D12();
		void Initialize(catimer::TimerSystem* timer
			, ca_io::IOManager* ioManager
			, resource_management::ResourceManagingSystem* resourceManager
			, resource_management::ResourceImportingSystem* resourceImporter
			, castl::string const& appName
			, castl::string const& engineName) override;
		void Release() override;
		castl::shared_ptr<WindowHandle> GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window) override;
		bool AnyWindowRunning() override;
		virtual void ScheduleGPUFrame(TaskScheduler* scheduler, GPUFrame const& gpuFrame) override {}
		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(GPUBufferDescriptor const& descriptor) override;
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) override;
		virtual castl::shared_ptr<ShaderStruct> CreateShaderStruct(cacore::NameHash const& structType) override;

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
		ComPtr<ID3D12CommandQueue> GetDirectQueue() const
		{
			return m_CommandQueue;
		}

		MemoryManager& GetMemoryManager()
		{
			return m_MemoryManager;
		}

		SamplerManager& GetSamplerManager()
		{
			return m_SamplerManager;
		}

		RootSignatureManager& GetRootSignatureManager()
		{
			return m_RootSignatureManager;
		}

		GPUPipelineManager& GetRasterPipelineManager()
		{
			return m_PipelineManager;
		}

		GPUComputePipelineManager& GetComputePipelineManager()
		{
			return m_ComputePipelineManager;
		}

		bool DeviceInited() const
		{
			return !(m_Device == nullptr);
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
		castl::shared_ptr<T> NewSubObject_Shared(TArgs&&...Args) {
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
				static_assert(CanInit<T, TArgs...>);
				if constexpr (CanInit<T, TArgs...>)
				{
					newSubObject->Init(castl::forward<TArgs>(Args)...);
				}
				return newSubObject;
			}
		};

		ShaderFileInfo const* GetShaderFileInfo(ShaderInfo const& shaderInfo);

		ShaderSetData GetShaderCodes(ShaderInfo const& shaderInfo);

	private:
		castl::shared_list<D3D12SubobjectBase*> m_PendingInitializeObjects;
		ComPtr<IDXGIFactory4>		m_Factory = nullptr;
		ComPtr<ID3D12Device>		m_Device = nullptr;
		ComPtr<IDXGIAdapter1>		m_Adapter = nullptr;
		ComPtr<ID3D12CommandQueue>	m_CommandQueue = nullptr;

		MemoryManager m_MemoryManager;
		ca_io::IOManager* p_IOManager;
		resource_management::ResourceManagingSystem* p_ResourceManager;
		resource_management::ResourceImportingSystem* p_ResourceImporter;

		castl::unordered_map<castl::shared_ptr<cawindow::IWindow>, castl::shared_ptr<WindowContext>> m_WindowContexts;

		//D3D12ShaderObjectDic m_ShaderObjects;

		D3D12ShaderResourceImporter m_ShaderResourceImporter;
		SamplerManager m_SamplerManager;
		RootSignatureManager m_RootSignatureManager;
		GPUPipelineManager m_PipelineManager;
		GPUComputePipelineManager m_ComputePipelineManager;

		friend class D3D12SubobjectBase;
		void AddPendingSubobject(D3D12SubobjectBase* obj)
		{
			if (DeviceInited())
			{
				obj->DeviceInit();
				return;
			}
			if (!m_PendingInitializeObjects.push_back_if(obj, [&]()->bool
			{
				return !DeviceInited();
			}))
			{
				CA_ASSERT_BREAK(DeviceInited(), "Device Should Init Here Now!");
				obj->DeviceInit();
				return;
			}
		}
		void DeviceInitializeSubObjects()
		{
			m_PendingInitializeObjects.clear([&](D3D12SubobjectBase* obj)
			{
				obj->DeviceInit();
			});
		}
	};
}
