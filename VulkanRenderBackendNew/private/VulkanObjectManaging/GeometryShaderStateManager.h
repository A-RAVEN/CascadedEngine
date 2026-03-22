#pragma once
#include <Utils/VulkanIncludes.h>
#include <CACore/CASharedDic.h>
#include <Utils/VulkanSubobjectBase.h>
#include <PipelineStates/GeometryShaderStates.h>

namespace graphics_backend
{
	class FragmentOutputState : public VulkanSubobjectBase
	{
		FragmentOutputStateCache m_StateCache;
		vk::Pipeline m_Library = nullptr;
	public:
		void Init(FragmentOutputStateCache const& stateDesc);
		vk::Pipeline const& GetLibrary() const { return m_Library; }
		FragmentOutputStateCache const& GetCache() const { return m_StateCache; }
	};


	class VertexInputStateManager : public VulkanSubobjectBase
	{
	public:
		FragmentOutputState const& EnsureFragmentOutputState(FragmentOutputStateCache const& cacheData);
		FragmentOutputState const& GetVertexInputState(TypedVKHashVal<FragmentOutputStateCache> const& hashVal) const;
	private:
		castl::shared_dic<TypedVKHashVal<FragmentOutputStateCache>, FragmentOutputState> m_StateDesc;
	};
}