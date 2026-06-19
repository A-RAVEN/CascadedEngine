#pragma once
#include <cstring>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace graphics_backend
{
	inline void FillVmaVulkanFunctions(VmaVulkanFunctions& out)
	{
		memset(&out, 0, sizeof(out));
		out.vkGetInstanceProcAddr = vk::defaultDispatchLoaderDynamic.vkGetInstanceProcAddr;
		out.vkGetDeviceProcAddr = vk::defaultDispatchLoaderDynamic.vkGetDeviceProcAddr;
	}
}
