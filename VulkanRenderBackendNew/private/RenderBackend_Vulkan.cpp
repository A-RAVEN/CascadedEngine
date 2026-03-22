#include <RenderBackend_Vulkan.h>
#include <Utils/VulkanDebug.h>

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

	}
	void RenderBackend_Vulkan::ExecuteGraph(TaskScheduler* scheduler, castl::shared_ptr<GPUGraph> const& graph)
	{
	}
	void RenderBackend_Vulkan::Release()
	{
	}
	castl::shared_ptr<GPUBuffer> RenderBackend_Vulkan::CreateGPUBuffer(GPUBufferDescriptor const& descriptor, EBufferUsageFlags usageFlags)
	{
		return castl::shared_ptr<GPUBuffer>();
	}
	castl::shared_ptr<GPUTexture> RenderBackend_Vulkan::CreateGPUTexture(GPUTextureDescriptor const& inDescriptor, ETextureAccessTypeFlags accessType)
	{
		return castl::shared_ptr<GPUTexture>();
	}
	castl::shared_ptr<ShaderStruct> RenderBackend_Vulkan::CreateShaderStruct(cacore::NameHash const& structType)
	{
		return castl::shared_ptr<ShaderStruct>();
	}
	castl::shared_ptr<WindowHandle> RenderBackend_Vulkan::GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window)
	{
		return castl::shared_ptr<WindowHandle>();
	}
	bool RenderBackend_Vulkan::AnyWindowRunning()
	{
		return false;
	}
}

CA_MODULE_INSTANCE(graphics_backend::CRenderBackend, graphics_backend::RenderBackend_Vulkan, RenderBackend_Vulkan);