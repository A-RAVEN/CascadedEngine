#include "pch.h"

//Dynamic Function Pointers of Vulkan Should be defined under global namespace
PFN_vkCreateDebugUtilsMessengerEXT  pfnVkCreateDebugUtilsMessengerEXT = nullptr;
PFN_vkDestroyDebugUtilsMessengerEXT pfnVkDestroyDebugUtilsMessengerEXT = nullptr;
PFN_vkSetDebugUtilsObjectNameEXT pfnVkSetDebugUtilsObjectNameEXT = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance                                 instance,
	const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkDebugUtilsMessengerEXT* pMessenger)
{
	return pfnVkCreateDebugUtilsMessengerEXT(instance, pCreateInfo, pAllocator, pMessenger);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, VkAllocationCallbacks const* pAllocator)
{
	return pfnVkDestroyDebugUtilsMessengerEXT(instance, messenger, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo)
{
	return pfnVkSetDebugUtilsObjectNameEXT(device, pNameInfo);
}


namespace vulkan_backend
{
    namespace utils
    {
		void SetupVulkanInstanceFunctionPointers(vk::Instance const& inInstance)
		{
			pfnVkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(inInstance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));
			pfnVkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(inInstance.getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
		}

		void SetupVulkanDeviceFunctinoPointers(vk::Device const& inDevice)
		{
			pfnVkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(inDevice.getProcAddr("vkSetDebugUtilsObjectNameEXT"));
		}

		void CleanupVulkanInstanceFuncitonPointers()
		{
			pfnVkCreateDebugUtilsMessengerEXT = nullptr;
			pfnVkDestroyDebugUtilsMessengerEXT = nullptr;
		}



		vk::ImageSubresourceRange const& DefaultColorSubresourceRange()
		{
			const static vk::ImageSubresourceRange s_DefaultColorSubresourceRange = {
				vk::ImageAspectFlagBits::eColor
				, 0
				, 1
				, 0
				, 1
			};
			return s_DefaultColorSubresourceRange;
		}

		vk::ImageSubresourceRange const& DefaultDepthSubresourceRange()
		{
			const static vk::ImageSubresourceRange s_DefaultColorSubresourceRange = {
				vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil
				, 0
				, 1
				, 0
				, 1
			};
			return s_DefaultColorSubresourceRange;
		}

		vk::ImageSubresourceRange MakeSubresourceRange(ETextureFormat format
			, uint32_t baseMip
			, uint32_t mipCount
			, uint32_t baseLayer
			, uint32_t layerCount)
		{
			bool isDepthOnly = IsDepthOnlyFormat(format);
			bool isDepthStencil = IsDepthStencilFormat(format);
			bool hasStencil = isDepthStencil && !isDepthOnly;
			vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor;
			if (isDepthStencil)
			{
				aspectFlags = vk::ImageAspectFlagBits::eDepth;
				if(hasStencil)
				{
					aspectFlags |= vk::ImageAspectFlagBits::eStencil;
				}
			}
			vk::ImageSubresourceRange result = {
				aspectFlags
				, baseMip
				, mipCount
				, baseLayer
				, layerCount
			};
			return result;
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

	        std::cerr << vk::to_string(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(messageSeverity)) << ": "
	            << vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageTypes)) << ":\n";
	        std::cerr << std::string("\t") << "messageIDName   = <" << pCallbackData->pMessageIdName << ">\n";
	        std::cerr << std::string("\t") << "messageIdNumber = " << pCallbackData->messageIdNumber << "\n";
	        std::cerr << std::string("\t") << "message         = <" << pCallbackData->pMessage << ">\n";
	        if (0 < pCallbackData->queueLabelCount)
	        {
	            std::cerr << std::string("\t") << "Queue Labels:\n";
	            for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++)
	            {
	                std::cerr << std::string("\t\t") << "labelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
	            }
	        }
	        if (0 < pCallbackData->cmdBufLabelCount)
	        {
	            std::cerr << std::string("\t") << "CommandBuffer Labels:\n";
	            for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++)
	            {
	                std::cerr << std::string("\t\t") << "labelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
	            }
	        }
	        if (0 < pCallbackData->objectCount)
	        {
	            std::cerr << std::string("\t") << "Objects:\n";
	            for (uint32_t i = 0; i < pCallbackData->objectCount; i++)
	            {
	                std::cerr << std::string("\t\t") << "Object " << i << "\n";
	                std::cerr << std::string("\t\t\t") << "objectType   = " << vk::to_string(static_cast<vk::ObjectType>(pCallbackData->pObjects[i].objectType))
	                    << "\n";
	                std::cerr << std::string("\t\t\t") << "objectHandle = " << pCallbackData->pObjects[i].objectHandle << "\n";
	                if (pCallbackData->pObjects[i].pObjectName)
	                {
	                    std::cerr << std::string("\t\t\t") << "objectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
	                }
	            }
	        }
			if (messageSeverity == VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
#if BREAK_ON_VULKAN_ERROR
				__debugbreak();
#endif
			}
	        return VK_FALSE;
	    }

		vk::DebugUtilsMessengerCreateInfoEXT makeDebugUtilsMessengerCreateInfoEXT()
		{
			return { {},
					 vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
					 vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
					   vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
					 &debugUtilsMessengerCallback };
		}
    }

}