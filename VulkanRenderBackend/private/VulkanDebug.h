#pragma once
#include "VulkanIncludes.h"

namespace vulkan_backend
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
}