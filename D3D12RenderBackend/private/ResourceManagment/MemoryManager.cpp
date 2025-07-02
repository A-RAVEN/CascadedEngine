#include "MemoryManager.h"
#include <D3D12Debug.h>
#include <RenderBackend_D3D12.h>

namespace graphics_backend
{
	MemoryManager::MemoryManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app)
	{
		app->OnDeviceInit([&]()
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
		});
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

	GPUResource MemoryManager::AllocUploadStagingBuffer(uint64_t bufferSize)
	{
		D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_NONE);
		D3D12MA::ALLOCATION_DESC allocationDesc = {};
		allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

		D3D12MA::Allocation* allocation;
		HRESULT hr = m_Allocator->CreateResource(
			&allocationDesc,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			NULL,
			&allocation,
			IID_NULL, NULL);

		GPUResource result(this);
		result.SetAllocation(allocation);
		return result;
	}

	LinearMemoryManager::LinearMemoryManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) 
	{
		app->OnDeviceInit([&]()
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
		});
	}

	void LinearMemoryManager::Release()
	{
		Reset();
		m_Allocator.Reset();
	}

	ID3D12Resource* LinearMemoryManager::AllocUploadStagingResource(D3D12_RESOURCE_DESC const& resourceDesc)
	{
		D3D12MA::ALLOCATION_DESC allocationDesc = {};
		allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

		D3D12MA::Allocation* allocation;
		HRESULT hr = m_Allocator->CreateResource(
			&allocationDesc,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			&allocation,
			IID_NULL, NULL);
		m_Allocations.push_back(allocation);
		return allocation->GetResource();
	}

	ID3D12Resource* LinearMemoryManager::AllocUploadStagingBuffer(uint64_t bufferSize)
	{
		D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_NONE);
		D3D12MA::ALLOCATION_DESC allocationDesc = {};
		allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

		D3D12MA::Allocation* allocation;
		HRESULT hr = m_Allocator->CreateResource(
			&allocationDesc,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			&allocation,
			IID_NULL, NULL);
		m_Allocations.push_back(allocation);
		return allocation->GetResource();
	}
	void LinearMemoryManager::Reset()
	{
		for (D3D12MA::Allocation* allocation : m_Allocations)
		{
			allocation->Release();
		}
		m_Allocations.clear();
	}

	void AliasedGPUResource::FreeVirtualMemmories()
	{
		p_OwningBlock->FreeAllocation(m_Allocation);
	}

	ResourceInfo const& AliasedGPUResource::GetResourceInfo() const
	{
		return p_OwningAllocator->GetResourceInfo(m_HeapType, m_BlockID, m_ResourceID);
	}

	ID3D12Resource* AliasedGPUResource::GetResource() const
	{
		return p_OwningAllocator->GetResource(m_HeapType, m_BlockID, m_ResourceID);
	}

	AliasedMemoryAllocator::AliasedMemoryAllocator(RenderBackend_D3D12* app, ComPtr<D3D12MA::Allocator> allocator, uint64_t virtualBlockSize)
		: D3D12SubobjectBase(app), m_Allocator(allocator), m_VirtualBlockSize(virtualBlockSize)
	{
	}

	AliasedGPUResource AliasedMemoryAllocator::AllocateGPUResource(D3D12_RESOURCE_DESC const& resourceDesc, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState)
	{
		AliasedGPUResource result{};
		result.m_HeapType = heapType;
		auto found = m_Blocks.find(heapType);
		if (found == m_Blocks.end())
		{
			found = m_Blocks.insert(castl::make_pair(heapType, castl::vector<VirtualBlock>{})).first;
		}
		for (uint32_t blockID = 0; blockID < found->second.size(); ++blockID)
		{
			VirtualBlock& virtualBlock = found->second[blockID];
			if (virtualBlock.TryAllocateGPUResource(*this, resourceDesc, initialState, result))
			{
				result.m_BlockID = blockID;
				return result;
			}
		}
		found->second.push_back(VirtualBlock{ m_VirtualBlockSize });
		{
			bool newAlloc = found->second.back().TryAllocateGPUResource(*this, resourceDesc, initialState, result);
			assert(newAlloc);
			result.m_BlockID = found->second.size() - 1;
		}
		return result;
	}

	void AliasedMemoryAllocator::LogAllocatorStates()
	{
		for (auto& pair : m_Blocks)
		{
			D3D12MA::Statistics heapStats;
			for (VirtualBlock& block : pair.second)
			{
				if (block.m_MaxSize == 0)
					continue;
				castl::cout << "block Bytes:" << block.m_MaxSize << castl::endl;
				int id = 0;
				for (auto& resourceInfo : block.m_Resources)
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
			for (VirtualBlock& block : pair.second)
			{
				block.CommitBlock(pair.first, m_Allocator.Get(), GetDevice().Get());
			}
		}
	}

	void AliasedMemoryAllocator::FreeMemories()
	{
		for (auto& pair : m_Blocks)
		{
			for (VirtualBlock& block : pair.second)
			{
				block.FreeMemory();
			}
		}
	}

	void AliasedMemoryAllocator::Release()
	{
		for (auto& pair : m_Blocks)
		{
			for (VirtualBlock& block : pair.second)
			{
				block.Release();
			}
			pair.second.clear();
		}
	}

	ResourceInfo const& AliasedMemoryAllocator::GetResourceInfo(D3D12_HEAP_TYPE, uint32_t virtualBlockID, uint32_t resourceID) const
	{
		return m_Blocks.find(D3D12_HEAP_TYPE_DEFAULT)->second[virtualBlockID].m_Resources[resourceID];
	}

	ID3D12Resource* AliasedMemoryAllocator::GetResource(D3D12_HEAP_TYPE, uint32_t virtualBlockID, uint32_t resourceID) const
	{
		return m_Blocks.find(D3D12_HEAP_TYPE_DEFAULT)->second[virtualBlockID].m_BlockPlacedResources[resourceID].Get();
	}

	AliasedMemoryAllocator::VirtualBlock::VirtualBlock(uint64_t virtualBlockSize) : m_MaxAlignment(0), m_MaxSize(0)
	{
		D3D12MA::VIRTUAL_BLOCK_DESC blockDesc = {};
		blockDesc.Size = virtualBlockSize;
		ThrowIfFailed(CreateVirtualBlock(&blockDesc, &m_Block));
	}

	void AliasedMemoryAllocator::VirtualBlock::FreeMemory()
	{
		for (auto& res : m_BlockPlacedResources)
		{
			res->Release();
		}
		m_BlockPlacedResources.clear();
		if (p_BlockAllocation != nullptr)
		{
			p_BlockAllocation->Release();
			p_BlockAllocation = nullptr;
		}
	}

	void AliasedMemoryAllocator::VirtualBlock::Release()
	{
		FreeMemory();
		m_Resources.clear();
		m_Block->Clear();
		m_Block->Release();
	}

	bool AliasedMemoryAllocator::VirtualBlock::TryAllocateGPUResource(AliasedMemoryAllocator& owningAllocator
		, D3D12_RESOURCE_DESC const& resourceDesc
		, D3D12_RESOURCE_STATES initialState
		, AliasedGPUResource& outGPUResource)
	{
		D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = owningAllocator.GetDevice()->GetResourceAllocationInfo(0, 1, &resourceDesc);

		D3D12MA::VIRTUAL_ALLOCATION_DESC allocDesc = {};
		allocDesc.Size = allocationInfo.SizeInBytes; // 4 KB
		allocDesc.Alignment = allocationInfo.Alignment;

		D3D12MA::VirtualAllocation alloc;
		UINT64 allocOffset;
		auto hr = m_Block->Allocate(&allocDesc, &alloc, &allocOffset);
		if (SUCCEEDED(hr))
		{
			m_MaxAlignment = std::max(m_MaxAlignment, allocDesc.Alignment);
			m_MaxSize = std::max(m_MaxSize, allocOffset + allocDesc.Size);

			ResourceInfo resourceInfo;
			resourceInfo.m_Desc = resourceDesc;
			resourceInfo.m_Allocation = alloc;
			resourceInfo.m_InitialState = initialState;
			resourceInfo.m_Offset = allocOffset;
			resourceInfo.m_Size = allocDesc.Size;
			m_Resources.push_back(resourceInfo);

			outGPUResource.m_Allocation = alloc;
			outGPUResource.p_OwningAllocator = &owningAllocator;
			outGPUResource.p_OwningBlock = m_Block;
			outGPUResource.m_ResourceID = m_Resources.size() - 1;
			return true;
		}

		return false;
	}

	void AliasedMemoryAllocator::VirtualBlock::CommitBlock(D3D12_HEAP_TYPE heapType, D3D12MA::Allocator* allocator, ID3D12Device* device)
	{
		if (m_MaxSize > 0)
		{
			D3D12MA::ALLOCATION_DESC
				allocationDesc = {};
			allocationDesc.HeapType = heapType;
			allocationDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED | D3D12MA::ALLOCATION_FLAG_CAN_ALIAS;
			D3D12_RESOURCE_ALLOCATION_INFO resourceAllocationInfo = {};
			resourceAllocationInfo.Alignment = m_MaxAlignment;
			resourceAllocationInfo.SizeInBytes = m_MaxSize;
			ThrowIfFailed(allocator->AllocateMemory(&allocationDesc, &resourceAllocationInfo, &p_BlockAllocation));

			m_BlockPlacedResources.reserve(m_Resources.size());
			m_BlockPlacedResources.clear();
			for (auto& resourceInfo : m_Resources)
			{
				ComPtr<ID3D12Resource> resource;
				ThrowIfFailed(device->
					CreatePlacedResource(p_BlockAllocation->GetHeap()
						, p_BlockAllocation->GetOffset() + resourceInfo.m_Offset
						, &resourceInfo.m_Desc
						, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)));
				m_BlockPlacedResources.push_back(resource);
			}
		}
	}
}