#pragma once
#include <Hasher.h>
#include <Utils/VulkanIncludes.h>
namespace graphics_backend
{
	struct ShaderModuleCache
	{
		cacore::PathHash shaderPath;
		cacore::NameHash entryName;
	};
}