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
		//p_SetPool = GetGPUObjectManager().m_DescriptorSetPoolDic.GetOrCreate(desc.GetPoolDesc()).get();
	}

	void DescriptorSetAllocator::Release()
	{
		GetDevice().destroyDescriptorSetLayout(m_Layout);
	}

	//vk::DescriptorSet DescriptorSetAllocator::AllocateSet()
	//{
	//	return p_SetPool->AllocateSet(m_Layout);
	//}

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
		if (m_DescPools.empty())
		{
			CA_ASSERT(m_AllocationID == 0, "DescriptorPool is not initialized correctly");
			return;
		}
		uint32_t poolCount = (m_AllocationID + m_ChunkSize - 1) / m_ChunkSize;
		for (int i = 0; i < poolCount; ++i)
		{
			GetDevice().resetDescriptorPool(m_DescPools[i]);
		}
		m_AllocationID = 0;
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
	DescriptorSetThreadPool::DescriptorSetThreadPool(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
		m_DescPools.reserve(10);
	}
	DescriptorSetThreadPool::DescriptorSetThreadPool(DescriptorSetThreadPool&& other) noexcept : VKAppSubObjectBaseNoCopy(castl::move(other))
	{
		castl::lock_guard<castl::mutex> lock(other.m_Mutex);
		CA_ASSERT(other.m_AvailablePools.size() == other.m_DescPools.size(), "");
		m_AvailablePools = castl::move(other.m_AvailablePools);
		m_DescPools = castl::move(other.m_DescPools);
		m_DescPools.reserve(10);
	}
	castl::shared_ptr<DescriptorPoolDic> DescriptorSetThreadPool::AquirePool()
	{
		castl::unique_lock<castl::mutex> lock(m_Mutex);
		if (m_AvailablePools.empty())
		{
			if (m_DescPools.size() < 10)
			{
				uint32_t lastIndex = m_DescPools.size();
				m_DescPools.emplace_back(GetVulkanApplication());
				m_AvailablePools.push_back(lastIndex);
			}
			else
			{
				m_ConditionVariable.wait(lock, [this]()
					{
						//m_AvailablePools不是空的，或者线程管理器已经停止，不再等待
						return !m_AvailablePools.empty();
					});
			}
		}
		CA_ASSERT(!m_AvailablePools.empty(), "Pool is empty");
		uint32_t index = m_AvailablePools.back();
		m_AvailablePools.pop_back();
		DescriptorPoolDic* resultPool = &m_DescPools[index];
		return castl::shared_ptr<DescriptorPoolDic>(resultPool, [this, index](DescriptorPoolDic* released)
			{
				{
					castl::lock_guard<castl::mutex> lock(m_Mutex);
					CA_ASSERT(released == &m_DescPools[index], "Invalid Descriptor Pool");
					m_AvailablePools.push_back(index);
				}
				m_ConditionVariable.notify_one();
			});
	}
	void DescriptorSetThreadPool::ResetPool()
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		for (auto& pool : m_DescPools)
		{
			pool.Foreach([&](auto& desc, DescriptorPool* pool) { pool->ResetAll(); });
		}
	}
	void DescriptorSetThreadPool::ReleasePool()
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		for (auto& pool : m_DescPools)
		{
			pool.Clear();
		}
		m_DescPools.clear();
		m_AvailablePools.clear();
	}
}

