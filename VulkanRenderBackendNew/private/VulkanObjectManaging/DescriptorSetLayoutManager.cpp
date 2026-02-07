#include <VulkanObjectManaging/DescriptorSetLayoutManager.h>
#include <RenderBackend_Vulkan.h>

namespace graphics_backend
{
	TypedVKHashVal<DescriptorSetLayoutCache> DescriptorSetLayoutContainer::EnsureLayout(DescriptorSetLayoutCache const& cacheData)
	{
		TypedVKHashVal<DescriptorSetLayoutCache> val = VKHashFunc<DescriptorSetLayoutCache>(cacheData);
		auto resultItr = m_LayoutDic.get_or_create(val, [&]()
		{
			DescriptorSetLayout newLayout;
			GetApp()->InitSubObj(&newLayout, cacheData);
			return newLayout;
		});
		return resultItr->first;
	}
	vk::DescriptorSetLayout DescriptorSetLayoutContainer::GetLayout(TypedVKHashVal<DescriptorSetLayoutCache> const& hashVal) const
	{
		auto result = m_LayoutDic.try_get(hashVal);
		if(result != nullptr)
		{
			return result->Get();
		}
		return nullptr;
	}
	void DescriptorSetLayout::Init(DescriptorSetLayoutCache const& layoutDesc)
	{
		castl::vector<vk::DescriptorSetLayoutBinding> bindings;
		bindings.reserve(layoutDesc.bindings.size());
		for (auto const& bindingCache : layoutDesc.bindings)
		{
			vk::DescriptorSetLayoutBinding binding{};
			binding.binding = bindingCache.binding;
			binding.descriptorType = bindingCache.descriptorType;
			binding.descriptorCount = bindingCache.descriptorCount;
			binding.stageFlags = bindingCache.stageFlags;
			bindings.push_back(binding);
		}
		vk::DescriptorSetLayoutCreateInfo layoutCreateInfo{};
		layoutCreateInfo.flags = layoutDesc.flags;
		layoutCreateInfo.setBindings(bindings);
		m_Layout = GetDevice().createDescriptorSetLayout(layoutCreateInfo);
	}
}