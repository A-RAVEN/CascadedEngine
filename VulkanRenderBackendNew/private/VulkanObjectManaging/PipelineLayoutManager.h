#pragma once
#include <Utils/VulkanIncludes.h>
#include <CACore/CASharedDic.h>
#include <Utils/VulkanSubobjectBase.h>
#include <PipelineStates/PipelineLayout.h>

namespace graphics_backend
{
	class PipelineLayout : public VulkanSubobjectBase
	{
		PipelineLayoutCache m_LayoutCache;
		vk::PipelineLayout m_Layout = nullptr;
	public:
		void Init(PipelineLayoutCache const& layoutDesc);
		vk::PipelineLayout const& Get() const { return m_Layout; }
	};

	class PipelineLayoutContainer : public VulkanSubobjectBase
	{
	public:
		TypedVKHashVal<PipelineLayoutCache>  EnsureLayout(PipelineLayoutCache const& cacheData);
		vk::PipelineLayout GetLayout(TypedVKHashVal<PipelineLayoutCache> const& hashVal) const;
	private:
		castl::shared_dic<TypedVKHashVal<PipelineLayoutCache>, PipelineLayout> m_LayoutDic;
	};
}