#include "pch.h"
#define BREAK_ON_VULKAN_ERROR 1
//Dynamic Function Pointers of Vulkan Should be defined under global namespace

PFN_vkCreateDebugUtilsMessengerEXT  pfnvkCreateDebugUtilsMessengerEXT = nullptr;
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance                                 instance,
	const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkDebugUtilsMessengerEXT* pMessenger)
{
	return pfnvkCreateDebugUtilsMessengerEXT(instance, pCreateInfo, pAllocator, pMessenger);
}

PFN_vkDestroyDebugUtilsMessengerEXT pfnvkDestroyDebugUtilsMessengerEXT = nullptr;
VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, VkAllocationCallbacks const* pAllocator)
{
	return pfnvkDestroyDebugUtilsMessengerEXT(instance, messenger, pAllocator);
}

PFN_vkSetDebugUtilsObjectNameEXT pfnvkSetDebugUtilsObjectNameEXT = nullptr;
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo)
{
	return pfnvkSetDebugUtilsObjectNameEXT(device, pNameInfo);
}

PFN_vkCmdBeginDebugUtilsLabelEXT pfnvkCmdBeginDebugUtilsLabelEXT = nullptr;
VKAPI_ATTR void VKAPI_CALL vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo)
{
	pfnvkCmdBeginDebugUtilsLabelEXT(commandBuffer, pLabelInfo);
}

PFN_vkCmdEndDebugUtilsLabelEXT pfnvkCmdEndDebugUtilsLabelEXT = nullptr;
VKAPI_ATTR void VKAPI_CALL vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer commandBuffer)
{
	pfnvkCmdEndDebugUtilsLabelEXT(commandBuffer);
}

#define LOAD_VULKAN_FUNCTION_POINTER(instance, function) pfn##function = reinterpret_cast<PFN_##function>(instance.getProcAddr(#function))

namespace vulkan_backend
{
    namespace utils
    {
		void SetupVulkanInstanceFunctionPointers(vk::Instance const& inInstance)
		{
			LOAD_VULKAN_FUNCTION_POINTER(inInstance, vkCreateDebugUtilsMessengerEXT);
			LOAD_VULKAN_FUNCTION_POINTER(inInstance, vkDestroyDebugUtilsMessengerEXT);
		}

		void SetupVulkanDeviceFunctinoPointers(vk::Device const& inDevice)
		{
			LOAD_VULKAN_FUNCTION_POINTER(inDevice, vkSetDebugUtilsObjectNameEXT);
			LOAD_VULKAN_FUNCTION_POINTER(inDevice, vkCmdBeginDebugUtilsLabelEXT);
			LOAD_VULKAN_FUNCTION_POINTER(inDevice, vkCmdEndDebugUtilsLabelEXT);
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