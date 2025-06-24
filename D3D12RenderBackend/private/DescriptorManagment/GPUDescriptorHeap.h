#pragma once
#include <D3D12Includes.h>
#include <Utils/D3D12SubobjectBase.h>
#include <CASTL/CARange.h>
#include <CASTL/CADeque.h>
#include <TextureSampler.h>
#include <CASTL/CAUnorderedMap.h>

namespace graphics_backend
{
	class DescriptorHeapAllocator;
	class DescriptorAllocation
	{
	public:
		DescriptorAllocation() = default;
		DescriptorAllocation(castl::range<uint32_t> const& range, DescriptorHeapAllocator* allocator) : m_Range(range), m_Allocator(allocator) {}
		DescriptorAllocation Split(uint32_t count);
		DescriptorAllocation Slice(uint32_t index);
		CD3DX12_CPU_DESCRIPTOR_HANDLE CPUHandle() const;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GPUHandle() const;
		bool IsValid() const;
		void Release();
	private:
		castl::range<uint32_t> m_Range;
		DescriptorHeapAllocator* m_Allocator;
	};

	//Free list based descriptor heap allocator
	class DescriptorHeapAllocator : D3D12SubobjectBase
	{
	public:
		DescriptorHeapAllocator(RenderBackend_D3D12* app) : D3D12SubobjectBase(app), m_ShaderVisible(false){}
		DescriptorHeapAllocator(DescriptorHeapAllocator&& other) noexcept = default;
		void Init(D3D12_DESCRIPTOR_HEAP_TYPE heapType, bool shaderVisible, uint32_t size);
		void Release() override;
		bool CanAllocate(uint32_t descCount) const;
		DescriptorAllocation AllocDescriptors(uint32_t descCount);
		CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t offset) const;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t offset) const;
		void FreeDescriptors(castl::range<uint32_t> range);
		ComPtr<ID3D12DescriptorHeap> const& GetHeap() const
		{
			return m_DescriptorHeap;
		}
	private:
		ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
		castl::list<castl::range<uint32_t>> m_FreeList;
		bool m_ShaderVisible;
	};

	class CPUPagedDescriptorAllocator : D3D12SubobjectBase
	{
	public:
		CPUPagedDescriptorAllocator() = delete;
		CPUPagedDescriptorAllocator(RenderBackend_D3D12* app, D3D12_DESCRIPTOR_HEAP_TYPE heapType);
		DescriptorAllocation AllocDescriptors(uint32_t descCount);
	private:
		castl::deque<DescriptorHeapAllocator> m_Pages;
		D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType;
	};

	class GPUDescriptorHeap : D3D12SubobjectBase
	{
	public:
		GPUDescriptorHeap(RenderBackend_D3D12* app, D3D12_DESCRIPTOR_HEAP_TYPE heapType);
		void DeviceInit() override;
		DescriptorAllocation AllocDescriptorChunk(uint32_t descCount);
		ComPtr<ID3D12DescriptorHeap> const& GetHeap() const
		{
			return m_HugeHeap.GetHeap();
		}
	private:
		DescriptorHeapAllocator m_HugeHeap;
		D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType;
	};

	class CPUDescriptorAllocatorSet
	{
	public:
		CPUDescriptorAllocatorSet() = delete;
		CPUDescriptorAllocatorSet(RenderBackend_D3D12* app);
		CPUPagedDescriptorAllocator m_SRV_UAV_CBV_Allocator;
		CPUPagedDescriptorAllocator m_RTV_Allocator;
		CPUPagedDescriptorAllocator m_DSV_Allocator;
	};

	class SamplerManager : D3D12SubobjectBase
	{
	public:
		SamplerManager() = delete;
		SamplerManager(RenderBackend_D3D12* app);
		CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(TextureSamplerDescriptor const& samplerDesc);
		castl::unordered_map<TextureSamplerDescriptor, DescriptorAllocation> m_TextureSamplers;
		CPUPagedDescriptorAllocator m_Sampler_Allocator;
	};


}