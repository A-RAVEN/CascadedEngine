#pragma once
#include "VulkanIncludes.h"

namespace graphics_backend
{
	constexpr uint32_t FRAMEBOUND_RESOURCE_POOL_SWAP_COUNT_PER_CONTEXT = 3;
	constexpr uint32_t SWAPCHAIN_BUFFER_COUNT = 3;
	using FrameType = uint64_t;
	constexpr FrameType INVALID_FRAMEID = (castl::numeric_limits<FrameType>::max)();

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
		};
	}
}