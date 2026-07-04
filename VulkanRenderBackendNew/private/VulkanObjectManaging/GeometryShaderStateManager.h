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
		void Init(GeometryShaderStates const& stateDesc);
		vk::Pipeline const& GetLibrary() const { return m_Library; }
		GeometryShaderStates const& GetCache() const { return m_StateCache; }
	};


	class GeometryShaderStateManager : public VulkanSubobjectBase
	{
	public:
		GeometryShaderState const& EnsureGeometryShaderState(GeometryShaderStates const& cacheData);
		GeometryShaderState const& GetGeometryShaderState(TypedVKHashVal<GeometryShaderStates> const& hashVal) const;
	private:
		castl::shared_dic<TypedVKHashVal<GeometryShaderStates>, GeometryShaderState> m_StateDesc;
	};
}