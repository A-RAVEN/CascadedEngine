#include "MemoryManager.h"
#include <D3D12Debug.h>

namespace graphics_backend
{
	MemoryManager::MemoryManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app)
	{
	}

	void MemoryManager::Init()
	{
		D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
		allocatorDesc.pDevice = GetDevice<ID3D12Device>().Get();
		allocatorDesc.pAdapter = GetAdapter<IDXGIAdapter>().Get();
		// These flags are optional but recommended.
		allocatorDesc.Flags = D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED |
			D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;

		D3D12MA::Allocator* allocator;
		ThrowIfFailed(D3D12MA::CreateAllocator(&allocatorDesc, &allocator));
		m_Allocator = allocator;
	}

	void MemoryManager::Release()
	{
		m_Allocator.Reset();
	}

	D3D12MA::Allocation* MemoryManager::AllocMemory(D3D12_RESOURCE_ALLOCATION_INFO const& allocationInfo, D3D12_HEAP_TYPE heapType)
	{
		D3D12MA::Allocation* allocation;
		D3D12MA::ALLOCATION_DESC allocationDesc = {};
		allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		ThrowIfFailed(m_Allocator->AllocateMemory(&allocationDesc, &allocationInfo, &allocation));
		return allocation;
	}

	GPUResource MemoryManager::AllocGPUResource(D3D12_RESOURCE_DESC const& resourceDesc, D3D12_HEAP_TYPE heapType)
	{
		D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = GetDevice()->GetResourceAllocationInfo(0, 1, &resourceDesc);
		D3D12MA::ALLOCATION_DESC allocationDesc = {};
		allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

		D3D12MA::Allocation* allocation;
		HRESULT hr = m_Allocator->CreateResource(
			&allocationDesc,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			NULL,
			&allocation,
			IID_NULL, NULL);

		GPUResource result(this);
		result.SetAllocation(allocation);
		return result;
	}

	void AliasedGPUResource::FreeVirtualMemmories()
	{
		p_OwningBlock->FreeAllocation(m_Allocation);
	}

	AliasedMemoryAllocator::AliasedMemoryAllocator(RenderBackend_D3D12* app, ComPtr<D3D12MA::Allocator> allocator, uint64_t virtualBlockSize)
		: D3D12SubobjectBase(app), m_Allocator(allocator), m_VirtualBlockSize(virtualBlockSize)
	{
	}

	AliasedGPUResource AliasedMemoryAllocator::AllocateGPUResource(D3D12_RESOURCE_DESC const& resourceDesc, D3D12_HEAP_TYPE heapType)
	{
		AliasedGPUResource result{};
		auto found = m_Blocks.find(heapType);
		if (found == m_Blocks.end())
		{
			found = m_Blocks.insert(castl::make_pair(heapType, castl::vector<VirtualBlock>{})).first;
		}
		for (VirtualBlock& virtualBlock : found->second)
		{
			if (virtualBlock.TryAllocateGPUResource(*this, resourceDesc, result))
			{
				return result;
			}
		}
		found->second.push_back(VirtualBlock{ m_VirtualBlockSize });
		assert(found->second.back().TryAllocateGPUResource(resourceDesc, result));
		return result;
	}

	void AliasedMemoryAllocator::LogAllocatorStates()
	{
		for (auto& pair : m_Blocks)
		{
			D3D12MA::Statistics heapStats;
			for (VirtualBlock& block : pair.second)
			{
				D3D12MA::Statistics stats;
				block.m_Block->GetStatistics(&stats);
				if (stats.AllocationCount > 0)
				{
					castl::cout << "block Bytes:" << stats.BlockBytes << ";allocation bytes:" << stats.AllocationBytes << ";allocation count:" << castl::endl;
				}
				int id = 0;
				for (auto resourceInfo : block.m_Resources)
				{
					castl::cout << "resource" << id << " offset:" << resourceInfo.m_Offset << ";resource size:" << resourceInfo.m_Size << castl::endl;
					++id;
				}
			}
		}
	}

	void AliasedMemoryAllocator::CommitAllocations()
	{
		for (auto& pair : m_Blocks)
		{
			D3D12MA::Statistics heapStats;
			for (VirtualBlock& block : pair.second)
			{
				D3D12MA::Statistics stats;
				block.m_Block->GetStatistics(&stats);
				if (stats.AllocationCount > 0)
				{
					heapStats.AllocationBytes += stats.AllocationBytes;
					heapStats.AllocationCount += stats.AllocationCount;
					heapStats.BlockBytes += stats.BlockBytes;
					heapStats.BlockCount += stats.BlockCount;

					D3D12MA::Allocation* allocation;
					D3D12MA::ALLOCATION_DESC
					allocationDesc = {};
					allocationDesc.HeapType = pair.first;
					allocationDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED | D3D12MA::ALLOCATION_FLAG_CAN_ALIAS;
					D3D12_RESOURCE_ALLOCATION_INFO resourceAllocationInfo = {};
					resourceAllocationInfo.Alignment = block.m_MaxAlignment;
					resourceAllocationInfo.SizeInBytes = stats.BlockBytes;
					ThrowIfFailed(m_Allocator->AllocateMemory(&allocationDesc, &resourceAllocationInfo, &allocation));
					m_Allocations.push_back(allocation);

					for (auto& resourceInfo : block.m_Resources)
					{
						allocation->GetHeap();
						allocation->GetOffset();
						ComPtr<ID3D12Resource> resource;
						GetDevice()->
							CreatePlacedResource(allocation->GetHeap()
								, allocation->GetOffset() + resourceInfo.m_Offset
								, &resourceInfo.m_Desc
								, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
						m_PlacedResources.push_back(resource);
					}
				}

			}
		}
	}

	void AliasedMemoryAllocator::ReleaseAll()
	{
		for (auto& pair : m_Blocks)
		{
			for (VirtualBlock& block : pair.second)
			{
				block.Release();
			}
			pair.second.clear();
		}
		for (auto resource : m_PlacedResources)
		{
			resource.Reset();
		}
		m_PlacedResources.clear();
		for (auto allocation : m_Allocations)
		{
			allocation->Release();
		}
		m_Allocations.clear();
	}

	AliasedMemoryAllocator::VirtualBlock::VirtualBlock(uint64_t virtualBlockSize)
	{
		D3D12MA::VIRTUAL_BLOCK_DESC blockDesc = {};
		blockDesc.Size = virtualBlockSize;
		ThrowIfFailed(CreateVirtualBlock(&blockDesc, &m_Block));
	}

	void AliasedMemoryAllocator::VirtualBlock::Release()
	{
		m_Resources.clear();
		m_Block->Clear();
		m_Block->Release();
	}

	bool AliasedMemoryAllocator::VirtualBlock::TryAllocateGPUResource(AliasedMemoryAllocator& owningAllocator, D3D12_RESOURCE_DESC const& resourceDesc, AliasedGPUResource& outGPUResource)
	{
		D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = owningAllocator.GetDevice()->GetResourceAllocationInfo(0, 1, &resourceDesc);

		D3D12MA::VIRTUAL_ALLOCATION_DESC allocDesc = {};
		allocDesc.Size = allocationInfo.SizeInBytes; // 4 KB
		allocDesc.Alignment = allocationInfo.Alignment;
		m_MaxAlignment = std::max(m_MaxAlignment, allocDesc.Alignment);

		D3D12MA::VirtualAllocation alloc;
		UINT64 allocOffset;
		auto hr = m_Block->Allocate(&allocDesc, &alloc, &allocOffset);
		if (SUCCEEDED(hr))
		{
			ResourceInfo resourceInfo;
			resourceInfo.m_Desc = resourceDesc;
			resourceInfo.m_Allocation = alloc;
			resourceInfo.m_Offset = allocOffset;
			resourceInfo.m_Size = allocDesc.Size;
			m_Resources.push_back(resourceInfo);

			outGPUResource.m_Offset = allocOffset;
			outGPUResource.m_Size = allocDesc.Size;
			outGPUResource.m_Allocation = alloc;
			outGPUResource.p_OwningAllocator = &owningAllocator;
			outGPUResource.p_OwningBlock = m_Block;
			return true;
		}

		return false;
	}

}