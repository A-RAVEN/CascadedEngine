#include <VulkanObjectManaging/PipelineLayoutManager.h>
#include <RenderBackend_Vulkan.h>

namespace graphics_backend
{
	TypedVKHashVal<PipelineLayoutCache> PipelineLayoutContainer::EnsureLayout(PipelineLayoutCache const& cacheData)
	{
		TypedVKHashVal<PipelineLayoutCache> val = VKHashFunc<PipelineLayoutCache>(cacheData);
		auto resultItr = m_LayoutDic.get_or_create(val, [&]()
		{
			PipelineLayout newLayout;
			GetApp()->InitSubObj(&newLayout, cacheData);
			return newLayout;
		});
		return resultItr->first;
	}
	vk::PipelineLayout PipelineLayoutContainer::GetLayout(TypedVKHashVal<PipelineLayoutCache> const& hashVal) const
	{
		auto result = m_LayoutDic.try_get(hashVal);
		if (result != nullptr)
		{
			return result->Get();
		}
		return nullptr;
	}
	void PipelineLayout::Init(PipelineLayoutCache const& layoutDesc)
	{
		castl::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
		descriptorSetLayouts.reserve(layoutDesc.descriptorSetLayouts.size());
		for (auto const& layoutHash : layoutDesc.descriptorSetLayouts)
		{
			auto layout = GetApp()->GetDescriptorSetLayoutContainer().GetLayout(layoutHash);
			descriptorSetLayouts.push_back(layout);
		}
		vk::PipelineLayoutCreateInfo layoutCreateInfo{};
		layoutCreateInfo.flags = layoutDesc.flags;
		layoutCreateInfo.setPushConstantRanges(layoutDesc.pushConstantRanges);
		layoutCreateInfo.setSetLayouts(descriptorSetLayouts);
		m_Layout = GetDevice().createPipelineLayout(layoutCreateInfo);
	}
}