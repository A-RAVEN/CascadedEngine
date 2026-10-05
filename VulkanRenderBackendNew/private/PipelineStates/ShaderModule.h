#pragma once
#include <HashKeys.h>
#include <Utils/VulkanIncludes.h>
namespace graphics_backend
{
	struct ShaderModuleCache
	{
		cacore::PathHash shaderPath;
		cacore::NameHash entryName;
	};
}