#pragma once
#include <VulkanIncludes.h>
#include <VulkanApplicationSubobjectBase.h>
#include <HashPool.h>
#include <CASTL/CASet.h>

namespace graphics_backend
{
	struct DescriptorPoolDesc
	{
		uint32_t UBONum;
		uint32_t SSBONum;
		uint32_t TexNum;
		uint32_t SamplerNum;
		auto operator<=>(const DescriptorPoolDesc&) const = default;
	};

	struct DescriptorDesc
	{
		vk::DescriptorType descType;
		uint32_t bindingIndex;
		uint32_t arraySize;
		auto operator<=>(const DescriptorDesc&) const = default;
	};

	struct DescriptorSetDesc
	{
		castl::vector<DescriptorDesc> descs;
		auto operator<=>(const DescriptorSetDesc&) const = default;

		DescriptorPoolDesc GetPoolDesc() const
		{
			DescriptorPoolDesc result{};
			for (auto& desc : descs)
			{
				switch (desc.descType)
				{
				case vk::DescriptorType::eUniformBuffer:
					result.UBONum++;
					break;
				case vk::DescriptorType::eStorageBuffer:
					result.SSBONum++;
					break;
				case vk::DescriptorType::eSampledImage:
					result.TexNum++;
					break;
				case vk::DescriptorType::eSampler:
					result.SamplerNum++;
					break;
				}
			}
			return result;
		}
	};


	class DescriptorPool : public VKAppSubObjectBaseNoCopy
	{
	public:
		DescriptorPool(CVulkanApplication& application);
		void Create(DescriptorPoolDesc const& poolDesc);
		vk::DescriptorSet AllocateSet(vk::DescriptorSetLayout inLayout);
		void ResetAll();
		void Release();
	private:
		void IncrementPoolSize();
		vk::DescriptorPool GetOrNewPool();
		vk::DescriptorPool NewPool();
		uint32_t m_ChunkSize = 4;
		uint32_t m_AllocationID = 0;
		DescriptorPoolDesc m_PoolDesc;
		castl::vector<vk::DescriptorPool> m_DescPools;
	};

	using DescriptorPoolDic = HashPool<DescriptorPoolDesc, DescriptorPool>;

	class DescriptorSetAllocator : public VKAppSubObjectBaseNoCopy
	{
	public:
		DescriptorSetAllocator(CVulkanApplication& application);
		void Create(DescriptorSetDesc const& desc);
		//vk::DescriptorSet AllocateSet();
		vk::DescriptorSetLayout GetLayout() const
		{
			return m_Layout;
		}
	private:
		vk::DescriptorSetLayout m_Layout;
		//DescriptorPool* p_SetPool;
	};

	using DescriptorSetAllocatorDic = HashPool<DescriptorSetDesc, DescriptorSetAllocator>;
}

