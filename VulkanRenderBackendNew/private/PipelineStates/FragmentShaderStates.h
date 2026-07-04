#pragma once
#include <PipelineStates/ShaderModule.h>
#include <Utils/VulkanIncludes.h>

namespace graphics_backend
{
	struct FragmentShaderStateCache
	{
		TypedVKHashVal<ShaderModuleCache> fragmentShaderCache;
	};
}