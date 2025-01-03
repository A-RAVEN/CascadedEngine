#pragma once

#include <Utils/D3D12SubobjectBase.h>
#include <ResourceManagment/GPUResource.h>
#include <CASTL/CANoCopy.h>
#include <CASTL/CAUnorderedMap.h>

namespace graphics_backend
{
	class AliasedMemoryAllocator;

	class MemoryManager : public D3D12SubobjectBase
	{
	public:
		MemoryManager(RenderBackend_D3D12* app);
		MemoryManager(MemoryManager&& other) = default;
		MemoryManager& operator=(MemoryManager&& other) = default;
		void Init();
		void Release() override;
		D3D12MA::Allocation* AllocMemory(D3D12_RESOURCE_ALLOCATION_INFO const& allocationInfo, D3D12_HEAP_TYPE heapType);
		GPUResource AllocGPUResource(D3D12_RESOURCE_DESC const& resourceDesc, D3D12_HEAP_TYPE heapType);
		ComPtr<D3D12MA::Allocator>& GetAllocator() { return m_Allocator; }
		ComPtr<D3D12MA::Allocator> const& GetAllocator() const { return m_Allocator; }
	private:
		ComPtr<D3D12MA::Allocator> m_Allocator;
	};

	struct ResourceInfo
	{
		D3D12_RESOURCE_DESC m_Desc;
		D3D12MA::VirtualAllocation m_Allocation;
		uint64_t m_Offset;
		uint64_t m_Size;
	};

	class AliasedGPUResource : public castl::nocopiable
	{
	public:
		void FreeVirtualMemmories();
		ResourceInfo const& GetResourceInfo() const;
	private:
		AliasedMemoryAllocator* p_OwningAllocator;
		D3D12MA::VirtualBlock* p_OwningBlock;
		D3D12MA::VirtualAllocation m_Allocation;

		D3D12_HEAP_TYPE m_HeapType;
		uint32_t m_BlockID;
		uint64_t m_ResourceID;

		friend class AliasedMemoryAllocator;
	};

	class AliasedMemoryAllocator : public D3D12SubobjectBase
	{
	public:


		class VirtualBlock
		{
		public:
			VirtualBlock(uint64_t virtualBlockSize);
			bool TryAllocateGPUResource(AliasedMemoryAllocator& owningAllocator, D3D12_RESOURCE_DESC const& resourceDesc, AliasedGPUResource& outGPUResource);
			void Release();
			D3D12MA::VirtualBlock* m_Block;
			castl::vector<ResourceInfo> m_Resources;
			uint64_t m_MaxAlignment = 0;
			uint64_t m_MaxSize = 0;
		};


		AliasedMemoryAllocator(RenderBackend_D3D12* app, ComPtr<D3D12MA::Allocator> allocator, uint64_t virtualBlockSize = (512 << 20));
		AliasedGPUResource AllocateGPUResource(D3D12_RESOURCE_DESC const& resourceDesc, D3D12_HEAP_TYPE heapType);
		void LogAllocatorStates();
		void CommitAllocations();
		void Release() override;
		
		ResourceInfo const& GetResourceInfo(D3D12_HEAP_TYPE, uint32_t virtualBlockID, uint32_t resourceID) const;

		castl::unordered_map<D3D12_HEAP_TYPE, castl::vector<VirtualBlock>> m_Blocks;
		uint64_t m_VirtualBlockSize;

		castl::vector<ComPtr<ID3D12Resource>> m_PlacedResources;
		castl::vector<D3D12MA::Allocation*> m_Allocations;
		ComPtr<D3D12MA::Allocator> m_Allocator;
	};
}