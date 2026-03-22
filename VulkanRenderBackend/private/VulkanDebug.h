#pragma once
#include "VulkanIncludes.h"

namespace graphics_backend
{

#define VK_RESULT_CHECK_LOG(result, log , ...) CA_ASSERT_BREAK((VkResult)result == VK_SUCCESS , log __VA_OPT__(, __VA_ARGS__ ));
#define VK_RESULT_CHECK(result) CA_ASSERT_BREAK((VkResult)result == VK_SUCCESS, "Vulkan Result Check Failed!");

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

}