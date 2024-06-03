#pragma once
#include "VulkanIncludes.h"

namespace graphics_backend
{
	template<typename T>
	void SetVKObjectDebugName(vk::Device device, T vkObj, const char* name)
	{
		if(device && vkObj && name)
		{
			using nativeType = typename T::NativeType;
			nativeType c_handle = vkObj;
			uint64_t handle = reinterpret_cast<uint64_t>(c_handle);
			vk::DebugUtilsObjectNameInfoEXT nameInfo;
			nameInfo.objectType = T::objectType;
			nameInfo.objectHandle = handle;
			nameInfo.pObjectName = name;
			device.setDebugUtilsObjectNameEXT(nameInfo);
		}
	}
	static void VKResultCheck(VkResult result, castl::string_view problem = "")
	{
		if (result != VK_SUCCESS)
		{
			CA_LOG_ERR("Vulkan Result Not Success!");
		}
	}
	static void VKResultCheck(vk::Result result, castl::string_view problem = "")
	{
		VKResultCheck(result, problem);
	}


}