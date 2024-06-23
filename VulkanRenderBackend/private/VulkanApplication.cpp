#include "pch.h"
#include <library_loader.h>
#include <FileLoader.h>
#include <MoveWrapper.h>
#include <CASTL/CASet.h>
#include "Utils.h"
#include "RenderBackendSettings.h"
#include "VulkanApplication.h"
#include "VulkanBarrierCollector.h"
#include "CommandList_Impl.h"
#include "InterfaceTranslator.h"
#include "GPUGraphExecutor/GPUGraphExecutor.h"
#include <GPUResources/VKGPUTexture.h>
#include <GPUResources/VKGPUBuffer.h>
#include <VulkanDebug.h>

namespace graphics_backend
{
	void CVulkanApplication::ScheduleGPUFrame(CTaskGraph* taskGraph, GPUFrame const& gpuFrame)
	{
		auto runGpuFrameTaskGraph = taskGraph->NewTaskGraph()
			->Name("Run GPU Frame")
			->SetupFunctor([this, gpuFrame](CTaskGraph* thisGraph)
			{
				auto tickWindowHandles = thisGraph->NewTask()
					->Name("TickWindow")
					->MainThread()
					->Functor([&]()
						{
							TickWindowContexts();
						});

				auto runGraph = thisGraph->NewTask()
					->Name("Run Graph")
					->DependsOn(tickWindowHandles)
					->Functor([&]()
						{
							auto frameManager = m_FrameContext.GetFrameBoundResourceManager();
							frameManager->releaseQueue.Load(m_GlobalResourceReleasingQueue);
							if (gpuFrame.pGraph != nullptr)
							{
								auto executor = frameManager->NewExecutor(gpuFrame.pGraph);
								executor->PrepareGraph();
							}
							if (gpuFrame.presentWindows.size() > 0)
							{
								for (auto& window : gpuFrame.presentWindows)
								{
									auto windowContext = castl::static_shared_pointer_cast<CWindowContext>(window);
									windowContext->PresentFrame(frameManager.get());
								}
							}
							frameManager->FinalizeSubmit();
						});
			});
	}

	GPUBuffer* CVulkanApplication::NewGPUBuffer(GPUBufferDescriptor const& inDescriptor)
	{
		VKGPUBuffer* result = new VKGPUBuffer(*this);
		result->SetDescriptor(inDescriptor);
		VKBufferObject bufferObject{};
		bufferObject.buffer = m_GPUResourceObjManager.CreateBuffer(inDescriptor);
		bufferObject.allocation = m_GPUMemoryManager.AllocateMemory(bufferObject.buffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
		m_GPUMemoryManager.BindMemory(bufferObject.buffer, bufferObject.allocation);
		result->SetBuffer(bufferObject);
		return result;
	}

	void CVulkanApplication::ReleaseGPUBuffer(GPUBuffer* releaseGPUBuffer)
	{
		VKGPUBuffer* vkBuffer = static_cast<VKGPUBuffer*>(releaseGPUBuffer);
		if (vkBuffer->Initialized())
		{
			m_GlobalResourceReleasingQueue.AddBuffers(vkBuffer->GetBuffer());
		}
	}

	GPUTexture* CVulkanApplication::NewGPUTexture(GPUTextureDescriptor const& inDescriptor)
	{
		VKGPUTexture* result = new VKGPUTexture(*this);
		result->SetDescriptor(inDescriptor);
		VKImageObject imageObject{};
		imageObject.image = m_GPUResourceObjManager.CreateImage(inDescriptor);
		imageObject.allocation = m_GPUMemoryManager.AllocateMemory(imageObject.image, vk::MemoryPropertyFlagBits::eDeviceLocal);
		m_GPUMemoryManager.BindMemory(imageObject.image, imageObject.allocation);
		result->SetImage(imageObject);
		return result;
	}

	void CVulkanApplication::ReleaseGPUTexture(GPUTexture* releaseGPUTexture)
	{
		VKGPUTexture* vkTexture = static_cast<VKGPUTexture*>(releaseGPUTexture);
		if (vkTexture->Initialized())
		{
			m_GlobalResourceReleasingQueue.AddImages(vkTexture->GetImage());
		}
	}

	//castl::shared_ptr<ShaderConstantSet> CVulkanApplication::NewShaderConstantSet(ShaderConstantsBuilder const& builder)
	//{
	//	auto subAllocator = m_ConstantSetAllocator.GetOrCreate(builder);
	//	return subAllocator->AllocateSet();
	//}

	//castl::shared_ptr<ShaderBindingSet> CVulkanApplication::NewShaderBindingSet(ShaderBindingBuilder const& builder)
	//{
	//	auto subAllocator = m_ShaderBindingSetAllocator.GetOrCreate(builder);
	//	return subAllocator->AllocateSet();
	//}

	void CVulkanApplication::InitializeInstance(castl::string const& name, castl::string const& engineName)
	{
		vk::ApplicationInfo application_info(
			name.c_str()
			, 1
			, engineName.c_str()
			, 0
			, VULKAN_API_VERSION_IN_USE);

		castl::array<const char*, 1> extensionNames = {
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		};

		const castl::vector<const char*> g_validationLayers{
			"VK_LAYER_KHRONOS_validation"
		};


		auto extensions = GetInstanceExtensionNames();
		vk::InstanceCreateInfo instance_info({}, &application_info, g_validationLayers, extensions);

#if !defined(NDEBUG)
		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsExt = vulkan_backend::utils::makeDebugUtilsMessengerCreateInfoEXT();
		instance_info.setPNext(&debugUtilsExt);
#endif
		m_Instance = vk::createInstance(instance_info);
		vulkan_backend::utils::SetupVulkanInstanceFunctionPointers(m_Instance);
	#if !defined(NDEBUG)
		m_DebugMessager = m_Instance.createDebugUtilsMessengerEXT(debugUtilsExt);
	#endif
	}

	void CVulkanApplication::DestroyInstance()
	{
		if (m_Instance != vk::Instance(nullptr))
		{
	#if !defined( NDEBUG )
			m_Instance.destroyDebugUtilsMessengerEXT(m_DebugMessager);
			m_DebugMessager = nullptr;
	#endif

			vulkan_backend::utils::CleanupVulkanInstanceFuncitonPointers();
			m_Instance.destroy();
			m_Instance = nullptr;
		}
	}

	void CVulkanApplication::EnumeratePhysicalDevices()
	{
		m_PhysicalDevice = m_Instance.enumeratePhysicalDevices().front();
		std::cout << "Device Name: " << m_PhysicalDevice.getProperties().deviceName << std::endl;
	}

	void CVulkanApplication::CreateDevice()
	{
		QueueContext::QueueCreationInfo queueCreationInfo{};
		m_QueueContext.InitQueueCreationInfo(m_PhysicalDevice, queueCreationInfo);
		auto extensions = GetDeviceExtensionNames();
		vk::DeviceCreateInfo deviceCreateInfo({}, queueCreationInfo.queueCreateInfoList, {}, extensions);
		m_Device = m_PhysicalDevice.createDevice(deviceCreateInfo);
		vulkan_backend::utils::SetupVulkanDeviceFunctinoPointers(m_Device);
	}

	void CVulkanApplication::DestroyDevice()
	{
		if(m_Device != vk::Device(nullptr))
		{
			m_Device.destroy();
			m_Device = nullptr;
		}
	}

	castl::shared_ptr<WindowHandle> CVulkanApplication::GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window)
	{
		m_WindowContexts.emplace_back(castl::make_shared<CWindowContext>(*this));
		auto newContext = m_WindowContexts.back();
		newContext->InitializeWindowHandle(window);
		return newContext;
	}

	void CVulkanApplication::TickWindowContexts()
	{
		size_t currentIndex = 0;
		while (currentIndex < m_WindowContexts.size())
		{
			if (m_WindowContexts[currentIndex]->Invalid())
			{
				m_WindowContexts[currentIndex]->ReleaseContext();
				castl::swap(m_WindowContexts[currentIndex], m_WindowContexts.back());
				m_WindowContexts.pop_back();
			}
			else
			{
				m_WindowContexts[currentIndex]->CheckRecreateSwapchain();
				++currentIndex;
			}
		}
	}

	void CVulkanApplication::ReleaseAllWindowContexts()
	{
		for (auto& windowContext : m_WindowContexts)
		{
			windowContext->ReleaseContext();
		}
		m_WindowContexts.clear();
	}

	CVulkanApplication::CVulkanApplication() :
	m_GPUObjectManager(*this)
	, m_QueueContext(*this)
	, m_FrameContext(*this)
	, m_GPUResourceObjManager(*this)
	, m_GPUMemoryManager(*this)
	, m_GlobalResourceReleasingQueue(*this)
	{
	}

	CVulkanApplication::~CVulkanApplication()
	{
	}

	void CVulkanApplication::InitApp(castl::string const& appName, castl::string const& engineName)
	{
		InitializeInstance(appName, engineName);
		EnumeratePhysicalDevices();
		CreateDevice();
		m_GPUMemoryManager.Initialize();
		m_GPUResourceObjManager.Initialize();
		m_FrameContext.InitFrameCapacity(4);

	}

	void CVulkanApplication::ReleaseApp()
	{
		DeviceWaitIdle();
		ReleaseAllWindowContexts();
		m_GlobalResourceReleasingQueue.ReleaseGlobalResources();
		m_FrameContext.Release();
		m_GPUResourceObjManager.Release();
		m_GPUMemoryManager.Release();
		DestroyDevice();
		m_PhysicalDevice = nullptr;
		DestroyInstance();
	}

	void CVulkanApplication::DeviceWaitIdle()
	{
		if (m_Device != vk::Device(nullptr))
		{
			m_Device.waitIdle();
		}
	}

}
