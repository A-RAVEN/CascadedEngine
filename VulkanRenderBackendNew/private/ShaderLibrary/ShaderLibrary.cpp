#include "ShaderLibrary.h"

namespace graphics_backend
{
	bool ShaderLibrary::TryAquireShaderModule(ShaderModuleCache const& cache
		, TypedVKHashVal<ShaderModuleCache>& outShaderModuleCache)
	{
		return false;
	}

	bool ShaderLibrary::ShaderModuleCacheValid(TypedVKHashVal<ShaderModuleCache> const& cache, VKShaderCodeHashVal const& shaderCodeHash) const
	{
		return false;
	}

	ShaderModuleCache ShaderLibrary::GetShaderModuleCache(TypedVKHashVal<ShaderModuleCache> const& cache) const
	{
		return {};
	}

	ShaderCodeSource ShaderLibrary::GetShaderCodeSource(TypedVKHashVal<ShaderModuleCache> const& cache) const
	{
		return {};
	}
}