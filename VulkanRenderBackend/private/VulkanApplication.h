#pragma once
#include <ThreadManager.h>
#include <ShaderBindingBuilder.h>
#include <CASTL/CAUnorderedMap.h>
#include <CRenderBackend.h>
#include "WindowContext.h"
#include "FrameCountContext.h"
#include "Containers.h"
#include "CShaderModuleObject.h"
#include "RenderPassObject.h"
#include "VulkanPipelineObject.h"
#include "FramebufferObject.h"
#include "IUploadingResource.h"
#include "GPUObjectManager.h"
#include "GPUGraphExecutor/GPUGraphExecutor.h"
#include <GPUContexts/QueueContext.h>
#include <GPUContexts/FrameContext.h>
#include <Utilities/SubobjectTraits.h>

namespace graphics_backend
{
	/*template<typename T, typename...TArgs>
	concept has_create = requires(T t)
	{
		t.Create(castl::remove_cvref_t <TArgs>{}...);
	};

	template<typename T, typename...TArgs>
	concept has_initialize = requires(T t)
	{
		t.Initialize(castl::remove_cvref_t<TArgs>{}...);
	};


	template<typename T>
	concept has_release = requires(T t)
	{
		t.Release();
	};

	template<typename T>
	struct SubObjectDefaultDeleter {
		void operator()(T* deleteObject)
		{
			if constexpr (has_release<T>)
			{
				deleteObject->Release();
			}
			delete deleteObject;
		}
	};*/

	using namespace thread_management;
	class CVulkanApplication
	{
	public:
		CVulkanApplication();
		~CVulkanApplication();
		void InitApp(castl::string const& appName, castl::string const& engineName);
		void ReleaseApp();
		void DeviceWaitIdle();
		inline vk::Instance const& GetInstance() const
		{
			return m_Instance;
		}
		inline vk::Device const& GetDevice() const
		{
			return m_Device;
		}
		inline vk::PhysicalDevice const& GetPhysicalDevice() const
		{
			return m_PhysicalDevice;
		}

		constexpr GPUObjectManager& GetGPUObjectManager() { return m_GPUObjectManager; }
		constexpr GPUMemoryResourceManager& GetGlobalMemoryManager() { return m_GPUMemoryManager; }
		constexpr GPUResourceObjectManager& GetGlobalResourceObjectManager() { return m_GPUResourceObjManager; }
		constexpr GlobalResourceReleaseQueue& GetGlobalResourecReleasingQueue() { return m_GlobalResourceReleasingQueue; }
		constexpr QueueContext& GetQueueContext() { return m_QueueContext; }
		bool AnyWindowRunning() const { return !m_WindowContexts.empty(); }
		castl::shared_ptr<WindowHandle> GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window);
		void TickWindowContexts();

		template<typename T, typename...TArgs>
		castl::shared_ptr<T> NewSubObject_Shared(TArgs&&...Args) {
			static_assert(castl::is_constructible_v<T, CVulkanApplication&> || castl::is_constructible_v<T, CVulkanApplication&, TArgs...>
				, "Type T Not Compatible To Vulkan SubObject");
			if constexpr (castl::is_constructible_v<T, CVulkanApplication&, TArgs...>)
			{
				castl::shared_ptr<T> newSubObject = castl::shared_ptr<T>{ new T(*this, castl::forward<TArgs>(Args)...), SubObjectDefaultDeleter<T>{} };
				if constexpr (has_initialize<T>)
				{
					newSubObject->Initialize();
				}
				else if constexpr (has_create<T>)
				{
					newSubObject->Create();
				}
				return newSubObject;
			}
			else
			{
				castl::shared_ptr<T> newSubObject = castl::shared_ptr<T>{ new T(*this), SubObjectDefaultDeleter<T>{} };
				if constexpr (has_initialize<T, TArgs...>)
				{
					newSubObject->Initialize(castl::forward<TArgs>(Args)...);
				}
				else if constexpr(has_create<T, TArgs...>)
				{
					newSubObject->Create(castl::forward<TArgs>(Args)...);
				}
				return newSubObject;
			}
		};

		void ScheduleGPUFrame(CTaskGraph* taskGraph, GPUFrame const& gpuFrame);
	public:
		//Allocation
		GPUBuffer* NewGPUBuffer(GPUBufferDescriptor const& inDescriptor);
		void ReleaseGPUBuffer(GPUBuffer* releaseGPUBuffer);

		GPUTexture* NewGPUTexture(GPUTextureDescriptor const& inDescriptor);
		void ReleaseGPUTexture(GPUTexture* releaseGPUTexture);

private:
		void InitializeInstance(castl::string const& name, castl::string const& engineName);
		void DestroyInstance();
		void EnumeratePhysicalDevices();
		void CreateDevice();
		void DestroyDevice();
		void ReleaseAllWindowContexts();
	private:
		vk::Instance m_Instance = nullptr;
		vk::PhysicalDevice m_PhysicalDevice = nullptr;
		vk::Device m_Device = nullptr;
	#if !defined(NDEBUG)
		vk::DebugUtilsMessengerEXT m_DebugMessager = nullptr;
	#endif
		castl::unordered_map<castl::shared_ptr<cawindow::IWindow>, castl::shared_ptr<CWindowContext>> m_WindowContexts;
		GPUObjectManager m_GPUObjectManager;
		GPUMemoryResourceManager m_GPUMemoryManager;
		GPUResourceObjectManager m_GPUResourceObjManager;
		GlobalResourceReleaseQueue m_GlobalResourceReleasingQueue;
		QueueContext m_QueueContext;
		FrameContext m_FrameContext;
	};
}
