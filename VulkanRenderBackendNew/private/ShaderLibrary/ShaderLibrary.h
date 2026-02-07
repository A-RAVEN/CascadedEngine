#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <Hasher.h>
#include <PipelineStates/ShaderModule.h>

namespace graphics_backend
{

	struct ShaderCodeSource
	{
		uint32_t shaderCodeLength;
		void* pShaderCode;
		VKShaderCodeHashVal shaderCodeHash;
	};

	class ShaderLibrary : public VulkanSubobjectBase
	{
	public:
		bool TryAquireShaderModule(ShaderModuleCache const& cache
			, TypedVKHashVal<ShaderModuleCache>& outShaderModuleCache);
		bool ShaderModuleCacheValid(TypedVKHashVal<ShaderModuleCache> const& cache, VKShaderCodeHashVal const& shaderCodeHash) const;
		ShaderModuleCache GetShaderModuleCache(TypedVKHashVal<ShaderModuleCache> const& cache) const;
		ShaderCodeSource GetShaderCodeSource(TypedVKHashVal<ShaderModuleCache> const& cache) const;
	};
}