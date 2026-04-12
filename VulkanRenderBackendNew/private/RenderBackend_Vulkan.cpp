#include <RenderBackend_Vulkan.h>
#include <Utils/VulkanDebug.h>
#include <VulkanObjects/VulkanBuffer.h>
#include <VulkanObjects/VulkanTexture.h>
#include <VulkanObjects/VulkanWindowHandle.h>
#include <VulkanObjects/VulkanShaderStruct.h>

#define CA_IMPLEMENT_MODULE 1
#include <CACore/CAModuleImplementation.h>

/// <summary>
/// Default global dispatch loader for Vulkan functions
/// </summary>
namespace vk {
	DispatchLoaderDynamic defaultDispatchLoaderDynamic;
}

namespace graphics_backend
{

	static castl::vector<const char*> GetInstanceExtensionNames()
	{
		return castl::vector<const char*>{
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
				VK_KHR_SURFACE_EXTENSION_NAME,
#if defined( VK_USE_PLATFORM_ANDROID_KHR )
				VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_IOS_MVK )
				VK_MVK_IOS_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_MACOS_MVK )
				VK_MVK_MACOS_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_MIR_KHR )
				VK_KHR_MIR_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_VI_NN )
				VK_NN_VI_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_WAYLAND_KHR )
				VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_WIN32_KHR )
				VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_XCB_KHR )
				VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_XLIB_KHR )
				VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_XLIB_XRANDR_EXT )
				VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME,
#endif
		};
	}

	static castl::vector<const char*> GetDeviceExtensionNames()
	{
		return castl::vector<const char*>{
			VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
			VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME,
		};
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT       messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT              messageTypes,
		VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
		void* /*pUserData*/)
	{
#if !defined( NDEBUG )
		if (pCallbackData->messageIdNumber == 648835635)
		{
			// UNASSIGNED-khronos-Validation-debug-build-warning-message
			return VK_FALSE;
		}
		if (pCallbackData->messageIdNumber == 767975156)
		{
			// UNASSIGNED-BestPractices-vkCreateInstance-specialuse-extension
			return VK_FALSE;
		}
#endif

		if (pCallbackData->pMessageIdName != NULL)
		{
			castl::stringstream messageStream;
			messageStream << vk::to_string(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(messageSeverity)) << ": "
				<< vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageTypes)) << ":\n";
			messageStream << std::string("\t") << "messageIDName   = <" << pCallbackData->pMessageIdName << ">\n";
			messageStream << std::string("\t") << "messageIdNumber = " << pCallbackData->messageIdNumber << "\n";
			messageStream << std::string("\t") << "message         = <" << pCallbackData->pMessage << ">\n";
			if (0 < pCallbackData->queueLabelCount)
			{
				messageStream << std::string("\t") << "Queue Labels:\n";
				for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++)
				{
					messageStream << std::string("\t\t") << "labelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
				}
			}
			if (0 < pCallbackData->cmdBufLabelCount)
			{
				messageStream << std::string("\t") << "CommandBuffer Labels:\n";
				for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++)
				{
					messageStream << std::string("\t\t") << "labelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
				}
			}
			if (0 < pCallbackData->objectCount)
			{
				messageStream << std::string("\t") << "Objects:\n";
				for (uint32_t i = 0; i < pCallbackData->objectCount; i++)
				{
					messageStream << std::string("\t\t") << "Object " << i << "\n";
					messageStream << std::string("\t\t\t") << "objectType   = " << vk::to_string(static_cast<vk::ObjectType>(pCallbackData->pObjects[i].objectType))
						<< "\n";
					messageStream << std::string("\t\t\t") << "objectHandle = " << pCallbackData->pObjects[i].objectHandle << "\n";
					if (pCallbackData->pObjects[i].pObjectName)
					{
						messageStream << std::string("\t\t\t") << "objectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
					}
				}
			}
			CA_LOG_ERR("///////////////////\n{}\n///////////////////", messageStream.str());
			if (messageSeverity == VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
#if BREAK_ON_VULKAN_ERROR
				__debugbreak();
#endif
			}
		}

		return VK_FALSE;
	}


	void RenderBackend_Vulkan::Init(cacore::IModuleManager* pModuleManager)
	{
		p_ResourceManager = pModuleManager->GetInstance<resource_management::ResourceManagingSystem>();

		vk::defaultDispatchLoaderDynamic.init();

		vk::ApplicationInfo application_info(
			"Test Backend"
			, 1
			, "Test Engine"
			, 0
			, VULKAN_API_VERSION_IN_USE);

		const castl::vector<const char*> g_validationLayers{
			 "VK_LAYER_KHRONOS_validation"
		};

		auto extensions = GetInstanceExtensionNames();
		vk::InstanceCreateInfo instance_info({}, &application_info, g_validationLayers, extensions);

#if !defined(NDEBUG)
		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsExt{ {},
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
			| vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
			| vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
			| vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
			&debugUtilsMessengerCallback };
		instance_info.setPNext(&debugUtilsExt);
#endif
		m_VulkanInstance = vk::createInstance(instance_info);
		vk::defaultDispatchLoaderDynamic.init(m_VulkanInstance);
#if !defined(NDEBUG)
		m_DebugMessenger = m_VulkanInstance.createDebugUtilsMessengerEXT(debugUtilsExt);
#endif
		//Init Device
		m_PhysicalDevice = m_VulkanInstance.enumeratePhysicalDevices().front();
		InitSubObj(&m_QueueContext);
		auto deviceExts = GetDeviceExtensionNames();
		QueueContext::QueueCreationInfo queueCreationInfo{};
		m_QueueContext.InitQueueCreationInfo(m_PhysicalDevice, queueCreationInfo);
		vk::DeviceCreateInfo deviceCreateInfo({}, queueCreationInfo.queueCreateInfoList, {}, deviceExts);
		m_Device = m_PhysicalDevice.createDevice(deviceCreateInfo);
		vk::defaultDispatchLoaderDynamic.init(m_Device);

		//Init Object Containers
		InitSubObj(&m_DescriptorSetLayoutContainer);

		// Init Memory Manager
		InitSubObj(&m_MemoryManager);
		m_MemoryManager.Init();

		// Init Command List Manager
		InitSubObj(&m_CommandListManager);
		m_CommandListManager.Init();

		// Check for pipeline library support
		auto deviceExtensions = m_PhysicalDevice.enumerateDeviceExtensionProperties();
		for (auto const& ext : deviceExtensions)
		{
			if (strcmp(ext.extensionName, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME) == 0)
			{
				m_PipelineLibrarySupported = true;
				CA_LOG_INFO("VK_EXT_graphics_pipeline_library supported");
				break;
			}
		}

		// Init Pipeline Library
		InitSubObj(&m_PipelineLibrary);
		m_PipelineLibrary.Init();

		// Init Pipeline Library Cache
		InitSubObj(&m_PipelineLibraryCache);
		m_PipelineLibraryCache.Init();

		CA_LOG_INFO("VulkanRenderBackend initialized successfully");
	}
	void RenderBackend_Vulkan::ExecuteGraph(TaskScheduler* scheduler, castl::shared_ptr<GPUGraph> const& graph)
	{
		if (!graph)
		{
			CA_LOG_WARN("RenderBackend_Vulkan::ExecuteGraph - null graph");
			return;
		}

		// Create graph executor
		VulkanGraphExecutor executor;
		InitSubObj(&executor);
		executor.Init();

		// Execute graph
		executor.CompileAndExecute(scheduler, graph);

		// Release executor
		executor.Release();
	}
	void RenderBackend_Vulkan::Release()
	{
		// Clear window handles
		m_WindowHandles.clear();

		// Release pipeline library cache
		m_PipelineLibraryCache.Release();

		// Release pipeline library
		m_PipelineLibrary.Release();

		// Release command list manager
		m_CommandListManager.Release();

		// Release memory manager
		m_MemoryManager.Release();

		// Release descriptor set layout container
		m_DescriptorSetLayoutContainer.Release();

		// Destroy device
		if (m_Device)
		{
			m_Device.waitIdle();
			m_Device.destroy();
			m_Device = nullptr;
		}

#if !defined(NDEBUG)
		// Destroy debug messenger
		if (m_DebugMessenger)
		{
			m_VulkanInstance.destroyDebugUtilsMessengerEXT(m_DebugMessenger);
			m_DebugMessenger = nullptr;
		}
#endif

		// Destroy instance
		if (m_VulkanInstance)
		{
			m_VulkanInstance.destroy();
			m_VulkanInstance = nullptr;
		}

		CA_LOG_INFO("VulkanRenderBackend released");
	}
	castl::shared_ptr<GPUBuffer> RenderBackend_Vulkan::CreateGPUBuffer(GPUBufferDescriptor const& descriptor, EBufferUsageFlags usageFlags)
	{
		auto buffer = castl::make_shared<VulkanBuffer>();
		InitSubObj(buffer.get());
		buffer->Init(descriptor, usageFlags);
		return buffer;
	}
	castl::shared_ptr<GPUTexture> RenderBackend_Vulkan::CreateGPUTexture(GPUTextureDescriptor const& inDescriptor, ETextureAccessTypeFlags accessType)
	{
		auto texture = castl::make_shared<VulkanTexture>();
		InitSubObj(texture.get());
		texture->Init(inDescriptor, accessType);
		return texture;
	}
	castl::shared_ptr<ShaderStruct> RenderBackend_Vulkan::CreateShaderStruct(cacore::NameHash const& structType)
	{
		auto shaderStruct = castl::make_shared<VulkanShaderStruct>();
		InitSubObj(shaderStruct.get());

		// Look up ShaderStructData from ShaderLibrary (references D3D12 pattern)
		ShaderCompilerSlang::ShaderStructData const* pData = nullptr;
		if (p_ResourceManager != nullptr)
		{
			auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("VulkanShaderLibrary.shLib");
			if (shaderLibrary != nullptr)
			{
				pData = shaderLibrary->GetShaderStruct(structType);
			}
		}

		shaderStruct->Init(pData);
		return shaderStruct;
	}

	VulkanShaderFileInfo const* RenderBackend_Vulkan::GetShaderFileInfo(ShaderInfo const& shaderInfo)
	{
		if (p_ResourceManager == nullptr)
			return nullptr;
		auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("VulkanShaderLibrary.shLib");
		if (shaderLibrary == nullptr)
			return nullptr;
		return shaderLibrary->GetShaderFileInfo(shaderInfo.path);
	}
	castl::shared_ptr<WindowHandle> RenderBackend_Vulkan::GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window)
	{
		if (!window)
			return nullptr;

		// Check for existing handle
		auto it = m_WindowHandles.find(window.get());
		if (it != m_WindowHandles.end())
		{
			auto existing = it->second.lock();
			if (existing)
				return existing;
		}

		// Create new handle
		auto windowHandle = castl::make_shared<VulkanWindowHandle>();
		InitSubObj(windowHandle.get());
		windowHandle->Init(window);

		m_WindowHandles[window.get()] = windowHandle;
		return windowHandle;
	}
	bool RenderBackend_Vulkan::AnyWindowRunning()
	{
		for (auto it = m_WindowHandles.begin(); it != m_WindowHandles.end(); )
		{
			auto handle = it->second.lock();
			if (!handle)
			{
				// Remove expired handles
				it = m_WindowHandles.erase(it);
			}
			else if (handle->IsValid())
			{
				return true;
			}
			else
			{
				++it;
			}
		}
		return false;
	}
}

CA_MODULE_INSTANCE(graphics_backend::CRenderBackend, graphics_backend::RenderBackend_Vulkan, RenderBackend_Vulkan);