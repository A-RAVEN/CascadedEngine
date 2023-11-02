#pragma once
#include "VulkanIncludes.h"
#include <RenderInterface/header/Common.h>
namespace vulkan_backend
{
	namespace utils
	{
		vk::DebugUtilsMessengerCreateInfoEXT makeDebugUtilsMessengerCreateInfoEXT();

		void SetupVulkanInstanceFunctionPointers(vk::Instance const& inInstance);
		void SetupVulkanDeviceFunctinoPointers(vk::Device const& inDevice);
		void CleanupVulkanInstanceFuncitonPointers();

		vk::ImageSubresourceRange const& DefaultColorSubresourceRange();
		vk::ImageSubresourceRange const& DefaultDepthSubresourceRange();
		vk::ImageSubresourceRange MakeSubresourceRange(ETextureFormat format
		, uint32_t baseMip = 0
		, uint32_t mipCount = 1
		, uint32_t baseLayer = 0
		, uint32_t layerCount = 1);
	}
}