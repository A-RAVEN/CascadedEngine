#pragma once
#include <Utils/VulkanIncludes.h>
#include <CACore/CASharedDic.h>
#include <Utils/VulkanSubobjectBase.h>
#include <PipelineStates/PipelineLayout.h>

namespace graphics_backend
{
	class DescriptorSetLayout : public VulkanSubobjectBase
	{
		DescriptorSetLayoutCache m_LayoutCache;
		vk::DescriptorSetLayout m_Layout = nullptr;
	public:
		void Init(DescriptorSetLayoutCache const& layoutDesc);
		vk::DescriptorSetLayout const& Get() const { return m_Layout; }
	};

	class DescriptorSetLayoutContainer : public VulkanSubobjectBase
	{
	public:
		TypedVKHashVal<DescriptorSetLayoutCache> EnsureLayout(DescriptorSetLayoutCache const& cacheData);
		vk::DescriptorSetLayout GetLayout(TypedVKHashVal<DescriptorSetLayoutCache> const& hashVal) const;
	private:
		castl::shared_dic<TypedVKHashVal<DescriptorSetLayoutCache>, DescriptorSetLayout> m_LayoutDic;
	};
}