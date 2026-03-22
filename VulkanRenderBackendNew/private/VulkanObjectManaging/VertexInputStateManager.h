#pragma once
#include <Utils/VulkanIncludes.h>
#include <CACore/CASharedDic.h>
#include <Utils/VulkanSubobjectBase.h>
#include <PipelineStates/VertexInputStates.h>

namespace graphics_backend
{

	class VertexInputState : public VulkanSubobjectBase
	{
		VertexInputStatesCache m_StateCache;
		vk::Pipeline m_VertexInputState = nullptr;
	public:
		void Init(VertexInputStatesCache const& stateDesc);
		vk::Pipeline const& Get() const { return m_VertexInputState; }
		VertexInputStatesCache const& GetCache() const { return m_StateCache; }
	};


	class VertexInputStateManager : public VulkanSubobjectBase
	{
	public:
		VertexInputState const& EnsureVertexInputState(VertexInputStatesCache const& cacheData);
		VertexInputState const& GetVertexInputState(TypedVKHashVal<VertexInputStatesCache> const& hashVal) const;
	private:
		castl::shared_dic<TypedVKHashVal<VertexInputStatesCache>, VertexInputState> m_StateDesc;
	};
}