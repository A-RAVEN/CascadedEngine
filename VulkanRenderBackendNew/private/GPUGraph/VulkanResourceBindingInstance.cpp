#include <GPUGraph/VulkanResourceBindingInstance.h>
#include <RenderBackend_Vulkan.h>

namespace graphics_backend
{
	void VulkanResourceBindingInstance::Init()
	{
		m_DescriptorSets.clear();
		m_PendingWrites.clear();
		m_BufferInfos.clear();
		m_ImageInfos.clear();
	}

	void VulkanResourceBindingInstance::Release()
	{
		// Note: Descriptor sets are freed when the pool is destroyed
		m_DescriptorSets.clear();
		m_PendingWrites.clear();
		m_BufferInfos.clear();
		m_ImageInfos.clear();
	}

	void VulkanResourceBindingInstance::SetUniformBuffer(uint32_t set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range)
	{
		m_BufferInfos.push_back({ buffer, offset, range });

		vk::WriteDescriptorSet write{};
		write.dstSet = m_DescriptorSets[set];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eUniformBuffer;
		write.pBufferInfo = &m_BufferInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetStorageBuffer(uint32_t set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range)
	{
		m_BufferInfos.push_back({ buffer, offset, range });

		vk::WriteDescriptorSet write{};
		write.dstSet = m_DescriptorSets[set];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eStorageBuffer;
		write.pBufferInfo = &m_BufferInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetSampledImage(uint32_t set, uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout, vk::Sampler sampler)
	{
		m_ImageInfos.push_back({ sampler, imageView, layout });

		vk::WriteDescriptorSet write{};
		write.dstSet = m_DescriptorSets[set];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		write.pImageInfo = &m_ImageInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetStorageImage(uint32_t set, uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout)
	{
		m_ImageInfos.push_back({ {}, imageView, layout });

		vk::WriteDescriptorSet write{};
		write.dstSet = m_DescriptorSets[set];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eStorageImage;
		write.pImageInfo = &m_ImageInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetSampler(uint32_t set, uint32_t binding, vk::Sampler sampler)
	{
		m_ImageInfos.push_back({ sampler, {}, {} });

		vk::WriteDescriptorSet write{};
		write.dstSet = m_DescriptorSets[set];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eSampler;
		write.pImageInfo = &m_ImageInfos.back();
		m_PendingWrites.push_back(write);
	}

	vk::DescriptorSet VulkanResourceBindingInstance::GetDescriptorSet(uint32_t set) const
	{
		auto it = m_DescriptorSets.find(set);
		if (it != m_DescriptorSets.end())
		{
			return it->second;
		}
		return nullptr;
	}

	bool VulkanResourceBindingInstance::AllocateDescriptorSets(vk::DescriptorPool pool, castl::vector<vk::DescriptorSetLayout> const& layouts)
	{
		if (layouts.empty())
			return true;

		auto device = GetDevice();

		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = pool;
		allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
		allocInfo.pSetLayouts = layouts.data();

		try
		{
			auto sets = device.allocateDescriptorSets(allocInfo);
			for (uint32_t i = 0; i < sets.size(); ++i)
			{
				m_DescriptorSets[i] = sets[i];
			}
			return true;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanResourceBindingInstance: Failed to allocate descriptor sets: {}", e.what());
			return false;
		}
	}

	void VulkanResourceBindingInstance::UpdateDescriptorSets()
	{
		if (m_PendingWrites.empty())
			return;

		GetDevice().updateDescriptorSets(m_PendingWrites, {});
		m_PendingWrites.clear();
		m_BufferInfos.clear();
		m_ImageInfos.clear();
	}
}
