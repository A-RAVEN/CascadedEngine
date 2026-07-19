#pragma once
#include <cstring>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace graphics_backend
{
	inline void FillVmaVulkanFunctions(VmaVulkanFunctions& out)
	{
		memset(&out, 0, sizeof(out));
		out.vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
		out.vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
	}
}
