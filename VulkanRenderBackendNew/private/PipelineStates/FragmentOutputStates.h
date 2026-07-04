#pragma once
#include <CASTL/CAVector.h>
#include <Utils/VulkanIncludes.h>

namespace graphics_backend
{
	//Vulkan Object
	struct FragmentOutputStateCache
	{
		castl::vector<vk::Format> colorFormats;
		vk::Format depthStencilFormat = vk::Format::eUndefined;
		vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
	};
}