#include <pch.h>
#include<VulkanApplication.h>
#include "DescriptorLayoutPool.h"


namespace graphics_backend
{
	DescriptorSetAllocator::DescriptorSetAllocator(CVulkanApplication& application) : VKAppSubObjectBaseNoCopy(application)
	{
	}

	void DescriptorSetAllocator::Create(DescriptorSetDesc const& desc)
	{
		castl::vector<vk::DescriptorSetLayoutBinding> bindings;
		bindings.resize(desc.descs.size());
		for (uint32_t i = 0; i < desc.descs.size(); ++i)
		{
			auto& binding = bindings[i];
			binding.binding = desc.descs[i].bindingIndex;
			binding.descriptorCount = desc.descs[i].arraySize;
			binding.descriptorType = desc.descs[i].descType;
			binding.stageFlags = vk::ShaderStageFlagBits::eAll;
		}
		vk::DescriptorSetLayoutCreateInfo layoutCreateInfo{ {}, bindings };
		m_Layout = GetDevice().createDescriptorSetLayout(layoutCreateInfo);
		p_SetPool = GetGPUObjectManager().m_DescriptorSetPoolDic.GetOrCreate(desc.GetPoolDesc()).get();
	}

	vk::DescriptorSet DescriptorSetAllocator::AllocateSet()
	{
		return p_SetPool->AllocateSet(m_Layout);
	}

	DescriptorPool::DescriptorPool(CVulkanApplication& application) : VKAppSubObjectBaseNoCopy(application)
	{
	}

	void DescriptorPool::Create(DescriptorPoolDesc const& poolDesc)
	{
		m_PoolDesc = poolDesc;
	}

	vk::DescriptorSet DescriptorPool::AllocateSet(vk::DescriptorSetLayout inLayout)
	{
		auto allocPool = GetOrNewPool();
		IncrementPoolSize();
		vk::DescriptorSetAllocateInfo allocInfo{
			allocPool
			, 1
			, &inLayout };
		vk::DescriptorSet resultSet = GetDevice().allocateDescriptorSets(allocInfo).front();
		return resultSet;
	}

	void DescriptorPool::ResetAll()
	{
		m_AllocationID = 0;
		if (m_DescPools.empty())
		{
			return;
		}
		uint32_t poolCount = m_AllocationID / m_ChunkSize;
		for (int i = 0; i <= poolCount; ++i)
		{
			GetDevice().resetDescriptorPool(m_DescPools[i]);
		}
	}

	void DescriptorPool::Release()
	{
		for (auto pool : m_DescPools)
		{
			GetDevice().destroyDescriptorPool(pool);
		}
	}

	void EmplaceDescriptorPoolSize(castl::vector<vk::DescriptorPoolSize>& inoutPoolSizes, vk::DescriptorType descType, uint32_t descNum, uint32_t chunkSize)
	{
		if (descNum > 0)
		{
			inoutPoolSizes.emplace_back(
				descType
				, descNum * chunkSize
			);
		}
	}

	void DescriptorPool::IncrementPoolSize()
	{
		++m_AllocationID;
	}

	vk::DescriptorPool DescriptorPool::GetOrNewPool()
	{
		uint32_t poolIndex = m_AllocationID / m_ChunkSize;
		if (poolIndex < m_DescPools.size())
		{
			return m_DescPools[poolIndex];
		}
		else
		{
			return NewPool();
		}
	}

	vk::DescriptorPool DescriptorPool::NewPool()
	{
		castl::vector<vk::DescriptorPoolSize> poolSizes{};
		poolSizes.reserve(3);
		EmplaceDescriptorPoolSize(poolSizes, vk::DescriptorType::eUniformBuffer, m_PoolDesc.UBONum, m_ChunkSize);
		EmplaceDescriptorPoolSize(poolSizes, vk::DescriptorType::eStorageBuffer, m_PoolDesc.SSBONum, m_ChunkSize);
		EmplaceDescriptorPoolSize(poolSizes, vk::DescriptorType::eSampler, m_PoolDesc.SamplerNum, m_ChunkSize);
		EmplaceDescriptorPoolSize(poolSizes, vk::DescriptorType::eSampledImage, m_PoolDesc.TexNum, m_ChunkSize);

		vk::DescriptorPoolCreateInfo poolInfo{ {}, m_ChunkSize, poolSizes };
		vk::DescriptorPool newPool = GetDevice().createDescriptorPool(poolInfo);
		m_DescPools.push_back(newPool);
		return newPool;
	}
}

