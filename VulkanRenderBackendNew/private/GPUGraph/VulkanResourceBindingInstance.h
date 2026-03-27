#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <ShaderStruct.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>

namespace graphics_backend
{
	// Shader resource binding for descriptor sets
	class VulkanResourceBindingInstance : public VulkanSubobjectBase
	{
	public:
		VulkanResourceBindingInstance() = default;
		~VulkanResourceBindingInstance() = default;

		void Init();
		virtual void Release() override;

		// Set resources
		void SetUniformBuffer(uint32_t set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range);
		void SetStorageBuffer(uint32_t set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range);
		void SetSampledImage(uint32_t set, uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout, vk::Sampler sampler);
		void SetStorageImage(uint32_t set, uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout);
		void SetSampler(uint32_t set, uint32_t binding, vk::Sampler sampler);

		// Get descriptor set for a set index
		vk::DescriptorSet GetDescriptorSet(uint32_t set) const;

		// Allocate descriptor sets from pool
		bool AllocateDescriptorSets(vk::DescriptorPool pool, castl::vector<vk::DescriptorSetLayout> const& layouts);

		// Update descriptor sets on GPU
		void UpdateDescriptorSets();

		// Get descriptor writes for batch update
		castl::vector<vk::WriteDescriptorSet> const& GetPendingWrites() const { return m_PendingWrites; }
		void ClearPendingWrites() { m_PendingWrites.clear(); }

	private:
		// Descriptor sets per set index
		castl::unordered_map<uint32_t, vk::DescriptorSet> m_DescriptorSets;

		// Pending descriptor writes
		castl::vector<vk::WriteDescriptorSet> m_PendingWrites;

		// Storage for descriptor info (must persist until update)
		castl::vector<vk::DescriptorBufferInfo> m_BufferInfos;
		castl::vector<vk::DescriptorImageInfo> m_ImageInfos;
	};
}
