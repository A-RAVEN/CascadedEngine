#pragma once
#include <PipelineStates/ShaderModule.h>
#include <Utils/VulkanIncludes.h>

namespace graphics_backend
{
	struct GeometryShaderStates
	{
		TypedVKHashVal<ShaderModuleCache> vertexShaderCache;
		TypedVKHashVal<ShaderModuleCache> tessControlShaderCache;
		TypedVKHashVal<ShaderModuleCache> tessEvalShaderCache;
		TypedVKHashVal<ShaderModuleCache> geometryShaderCache;
	};
}