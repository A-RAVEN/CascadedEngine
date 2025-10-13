#pragma once

#include <Utils/D3D12SubobjectBase.h>
#include <ResourceManagment/GPUResource.h>
#include <CASTL/CANoCopy.h>
#include <CASTL/CAUnorderedMap.h>

namespace graphics_backend
{
	class AliasedMemoryAllocator;

	//Common Immediate Memory Manager
	class MemoryManager : public D3D12SubobjectBase
	{
	public:
		MemoryManager(RenderBackend_D3D12* app);
		MemoryManager(MemoryManager&& other) = default;
		MemoryManager& operator=(MemoryManager&& other) = default;
		void Release() override;
		D3D12MA::Allocation* AllocMemory(D3D12_RESOURCE_ALLOCATION_INFO const& allocationInfo, D3D12_HEAP_TYPE heapType);
		GPUResource AllocGPUResource(D3D12_RESOURCE_DESC const& resourceDesc, D3D12_HEAP_TYPE heapType);
		GPUResource AllocUploadStagingBuffer(uint64_t bufferSize);

		ComPtr<D3D12MA::Allocator>& GetAllocator() { return m_Allocator; }
		ComPtr<D3D12MA::Allocator> const& GetAllocator() const { return m_Allocator; }
	private:
		ComPtr<D3D12MA::Allocator> m_Allocator;
	};

	class LinearMemoryManager : public D3D12SubobjectBase
	{
	public:
		LinearMemoryManager(RenderBackend_D3D12* app);
		LinearMemoryManager(LinearMemoryManager&& other) = default;
		void Release() override;
		ID3D12Resource* AllocUploadStagingResource(D3D12_RESOURCE_DESC const& resourceDesc);
		ID3D12Resource* AllocUploadStagingBuffer(uint64_t bufferSize);
		void Reset();
	private:
		ComPtr<D3D12MA::Allocator> m_Allocator;
		castl::vector<D3D12MA::Allocation*> m_Allocations;
	};

	struct ResourceInfo
	{
		D3D12_RESOURCE_DESC m_Desc;
		D3D12MA::VirtualAllocation m_Allocation;
		D3D12_RESOURCE_STATES m_InitialState;
		uint64_t m_Offset;
		uint64_t m_Size;
	};

	class AliasedGPUResource
	{
	public:
		void FreeVirtualMemmories();
		ResourceInfo const& GetResourceInfo() const;
		ID3D12Resource* GetResource() const;
		bool IsValid() const { return p_OwningAllocator != nullptr; }
	private:
		AliasedMemoryAllocator* p_OwningAllocator = nullptr;
		D3D12MA::VirtualBlock* p_OwningBlock = nullptr;
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
			bool TryAllocateGPUResource(AliasedMemoryAllocator& owningAllocator, D3D12_RESOURCE_DESC const& resourceDesc, D3D12_RESOURCE_STATES initialState, AliasedGPUResource& outGPUResource);
			void CommitBlock(D3D12_HEAP_TYPE heapType, D3D12MA::Allocator* allocator, ID3D12Device* device);
			void FreeMemory();
			void Reset();
			void Release();
			D3D12MA::VirtualBlock* m_Block;
			castl::vector<ResourceInfo> m_Resources;
			uint64_t m_MaxAlignment = 0;
			uint64_t m_MaxSize = 0;
			D3D12MA::Allocation* p_BlockAllocation;
			//ComPtr<ID3D12Heap> m_Heap;
			castl::vector<ComPtr<ID3D12Resource>> m_BlockPlacedResources;
		};


		AliasedMemoryAllocator(RenderBackend_D3D12* app, ComPtr<D3D12MA::Allocator> allocator, uint64_t virtualBlockSize = (512 << 20));
		AliasedMemoryAllocator(RenderBackend_D3D12* app, uint64_t virtualBlockSize = (512 << 20));
		AliasedGPUResource AllocateGPUResource(D3D12_RESOURCE_DESC const& resourceDesc, D3D12_HEAP_TYPE heapType
			, D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);
		void LogAllocatorStates();
		void CommitAllocations();
		//Only Free Memories
		void Reset();
		void Release() override;
		
		ResourceInfo const& GetResourceInfo(D3D12_HEAP_TYPE, uint32_t virtualBlockID, uint32_t resourceID) const;
		ID3D12Resource* GetResource(D3D12_HEAP_TYPE, uint32_t virtualBlockID, uint32_t resourceID) const;

		castl::unordered_map<D3D12_HEAP_TYPE, castl::vector<VirtualBlock>> m_Blocks;
		uint64_t m_VirtualBlockSize;

		ComPtr<D3D12MA::Allocator> m_Allocator;
	};
}