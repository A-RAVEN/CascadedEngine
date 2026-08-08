#pragma once
#include <Utils/VulkanIncludes.h>
#include <CACore/CASharedDic.h>
#include <Utils/VulkanSubobjectBase.h>
#include <PipelineStates/GeometryShaderStates.h>

namespace graphics_backend
{
	class GeometryShaderState : public VulkanSubobjectBase
	{
		GeometryShaderStates m_StateCache;
		vk::Pipeline m_Library = nullptr;
	public:
		// F31: Init removed — declared but never defined anywhere in the repo (dead code).
		vk::Pipeline const& GetLibrary() const { return m_Library; }
		GeometryShaderStates const& GetCache() const { return m_StateCache; }
	};


	class GeometryShaderStateManager : public VulkanSubobjectBase
	{
	public:
		// F31a: Ensure/Get declared but never defined (no .cpp) and never called —
		// same dead-code class as Init above. Kept for future GPL geometry work.
		GeometryShaderState const& EnsureGeometryShaderState(GeometryShaderStates const& cacheData);
		GeometryShaderState const& GetGeometryShaderState(TypedVKHashVal<GeometryShaderStates> const& hashVal) const;
	private:
		castl::shared_dic<TypedVKHashVal<GeometryShaderStates>, GeometryShaderState> m_StateDesc;
	};
}