#pragma once
#include <VulkanIncludes.h>
#include <VMA.h>

namespace graphics_backend
{
	struct VKBufferObject
	{
		vk::Buffer buffer;
		VmaAllocation allocation;

		constexpr bool IsValid() const
		{
			return buffer != vk::Buffer{nullptr} && allocation;
		}

		static VKBufferObject Default()
		{
			return VKBufferObject{ vk::Buffer{}, VmaAllocation{} };
		}
	};

	struct VKImageObject
	{
		vk::Image image;
		VmaAllocation allocation;

		constexpr bool IsValid() const
		{
			return image != vk::Image{ nullptr } && allocation;
		}

		static VKImageObject Default()
		{
			return VKImageObject{ vk::Image{}, VmaAllocation{} };
		}
	};
}