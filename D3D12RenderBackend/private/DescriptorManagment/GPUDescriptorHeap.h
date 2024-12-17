#pragma once
#include <D3D12Includes.h>
#include <Utils/D3D12SubobjectBase.h>
#include <CASTL/CARange.h>
#include <CASTL/CADeque.h>

namespace graphics_backend
{
	class DescriptorHeapAllocator;
	class DescriptorAllocation
	{
	public:
		DescriptorAllocation(castl::range<uint32_t> const& range, DescriptorHeapAllocator* allocator) : m_Range(range), m_Allocator(allocator) {}
		DescriptorAllocation Split(uint32_t count);

		void Release();
	private:
		castl::range<uint32_t> m_Range;
		DescriptorHeapAllocator* m_Allocator;
	};

	//Free list based descriptor heap allocator
	class DescriptorHeapAllocator : D3D12SubobjectBase
	{
	public:
		DescriptorHeapAllocator(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		DescriptorHeapAllocator(DescriptorHeapAllocator&& other) noexcept = default;
		void Init(D3D12_DESCRIPTOR_HEAP_TYPE heapType, bool shaderVisible, uint32_t size);
		void Release() override;
		bool CanAllocate(uint32_t descCount) const;
		DescriptorAllocation AllocDescriptors(uint32_t descCount);
		CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t offset) const;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t offset) const;
		void FreeDescriptors(castl::range<uint32_t> range);
	private:
		ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
		castl::list<castl::range<uint32_t>> m_FreeList;
	};

	class CPUPagedDescriptorAllocator : D3D12SubobjectBase
	{
	public:
		DescriptorAllocation AllocDescriptors(uint32_t descCount);
	private:
		castl::deque<DescriptorHeapAllocator> m_Pages;
	};

	class GPUDescriptorHeap : D3D12SubobjectBase
	{
	public:
		GPUDescriptorHeap(RenderBackend_D3D12* app) : D3D12SubobjectBase(app), m_HugeHeap(app){}
		void Init();
	private:
		DescriptorHeapAllocator m_HugeHeap;
	};
}