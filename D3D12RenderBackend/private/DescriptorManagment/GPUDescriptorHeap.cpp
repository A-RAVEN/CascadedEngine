#include "GPUDescriptorHeap.h"
#include <D3D12Debug.h>
#include <DebugUtils.h>

namespace graphics_backend
{
	void DescriptorHeapAllocator::Init(D3D12_DESCRIPTOR_HEAP_TYPE heapType, bool shaderVisible, uint32_t size)
	{
		m_ShaderVisible = shaderVisible;
		constexpr uint32_t kDescriptorHeapSize = castl::numeric_limits<uint32_t>::max();
		D3D12_DESCRIPTOR_HEAP_DESC m_DescriptorHeapDesc = {};
		m_DescriptorHeapDesc.Type = heapType;
		m_DescriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		m_DescriptorHeapDesc.NodeMask = 0;
		m_DescriptorHeapDesc.NumDescriptors = size;
		ThrowIfFailed(GetDevice()->CreateDescriptorHeap(&m_DescriptorHeapDesc, IID_PPV_ARGS(&m_DescriptorHeap)));
		m_FreeList.push_back({ 0, size });
	}

	void DescriptorHeapAllocator::Release()
	{
		m_DescriptorHeap.Reset();
	}

	bool DescriptorHeapAllocator::CanAllocate(uint32_t descCount) const
	{
		for (auto it = m_FreeList.begin(); it != m_FreeList.end(); ++it)
		{
			if (it->size() >= descCount)
			{
				return true;
			}
		}
		return false;
	}

	DescriptorAllocation DescriptorHeapAllocator::AllocDescriptors(uint32_t descCount)
	{
		for (auto it = m_FreeList.begin(); it != m_FreeList.end(); ++it)
		{
			if (it->size() >= descCount)
			{
				auto result = castl::range<uint32_t>(it->head(), it->head() + descCount);
				it->head() += descCount;
				if (it->size() == 0)
				{
					m_FreeList.erase(it);
				}
				return DescriptorAllocation(result, this);
			}
		}
		return DescriptorAllocation(castl::range<uint32_t>::invalid(), this);
	}
	CD3DX12_CPU_DESCRIPTOR_HANDLE DescriptorHeapAllocator::GetCPUHandle(uint32_t offset) const
	{
		auto stride = GetDevice()->GetDescriptorHandleIncrementSize(m_DescriptorHeap->GetDesc().Type);
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(
			m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart()
			, offset
			, stride);
	}
	CD3DX12_GPU_DESCRIPTOR_HANDLE DescriptorHeapAllocator::GetGPUHandle(uint32_t offset) const
	{
		CA_ASSERT_BREAK(m_ShaderVisible, "Only Shader Visible Allocator Can Have GPU Handle");
		auto stride = GetDevice()->GetDescriptorHandleIncrementSize(m_DescriptorHeap->GetDesc().Type);
		return CD3DX12_GPU_DESCRIPTOR_HANDLE(
			m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart()
			, offset
			, stride);
	}
	void DescriptorHeapAllocator::FreeDescriptors(castl::range<uint32_t> range)
	{
		if (range.size() == 0)
			return;
		auto itr = m_FreeList.begin();
		while (itr != m_FreeList.end())
		{
			if (itr->head() <= range.head())
			{
				break;
			}
			++itr;
		}
		if (itr == m_FreeList.end())
		{
			m_FreeList.push_back(range);
			itr = m_FreeList.end();
			--itr;
		}
		itr->expand(range);
		if (itr != m_FreeList.begin())
		{
			auto prev = itr;
			--prev;
			if (prev->can_combine(*itr))
			{
				prev->expand(*itr);
				m_FreeList.erase(itr);
				itr = prev;
			}
		}
		if (itr != m_FreeList.end())
		{
			auto next = itr;
			++next;
			if (next != m_FreeList.end() && itr->can_combine(*next))
			{
				itr->expand(*next);
				m_FreeList.erase(next);
			}
		}
	}
	DescriptorAllocation DescriptorAllocation::Split(uint32_t count)
	{
		CA_ASSERT_BREAK(m_Range.size() >= count, "Cannot Split Greater Allocation");
		DescriptorAllocation result{ castl::range<uint32_t>(m_Range.head(), m_Range.head() + count), m_Allocator };
		m_Range.head() += count;
		return result;
	}
	DescriptorAllocation DescriptorAllocation::Slice(uint32_t index)
	{
		CA_ASSERT_BREAK(m_Range.size() > index, "Descriptor Allocation Slice Index Out of Bounds[{}/{}]", m_Range.size(), index);
		DescriptorAllocation result{ castl::range<uint32_t>(m_Range.head() + index, m_Range.head() + index + 1), m_Allocator };
		return result;
	}
	CD3DX12_CPU_DESCRIPTOR_HANDLE DescriptorAllocation::CPUHandle() const
	{
		CA_ASSERT(m_Range.size() > 0, "Empty Descriptor Allocation");
		return m_Allocator->GetCPUHandle(m_Range.head());
	}
	CD3DX12_GPU_DESCRIPTOR_HANDLE DescriptorAllocation::GPUHandle() const
	{
		CA_ASSERT(m_Range.size() > 0, "Empty Descriptor Allocation");
		return m_Allocator->GetGPUHandle(m_Range.head());
	}
	bool DescriptorAllocation::IsValid() const
	{
		return m_Range.size() > 0;
	}
	void DescriptorAllocation::Release()
	{
		m_Allocator->FreeDescriptors(m_Range);
		m_Range = {};
	}

	CPUPagedDescriptorAllocator::CPUPagedDescriptorAllocator(RenderBackend_D3D12* app, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	 : D3D12SubobjectBase(app), m_HeapType(heapType) {}

	DescriptorAllocation CPUPagedDescriptorAllocator::AllocDescriptors(uint32_t descCount)
	{
		for (auto& page : m_Pages)
		{
			if (page.CanAllocate(descCount))
			{
				return page.AllocDescriptors(descCount);
			}
		}
		auto& newPage = m_Pages.emplace_front(GetApp());
		newPage.Init(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false, 1024);
		return newPage.AllocDescriptors(descCount);
	}

	GPUDescriptorHeap::GPUDescriptorHeap(RenderBackend_D3D12* app, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	 : D3D12SubobjectBase(app), m_HugeHeap(app), m_HeapType(heapType){}

	void GPUDescriptorHeap::DeviceInit()
	{
		m_HugeHeap.Init(m_HeapType, true, castl::numeric_limits<uint32_t>::max());
	}

	DescriptorAllocation GPUDescriptorHeap::AllocDescriptorChunk(uint32_t descCount)
	{
		return m_HugeHeap.AllocDescriptors(descCount);
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE SamplerManager::GetCPUHandle(TextureSamplerDescriptor const& samplerDesc)
	{
		auto found = m_TextureSamplers.find(samplerDesc);
		if (found != m_TextureSamplers.end())
		{
			return found->second.CPUHandle();
		}
		auto descAllocation =  m_Sampler_Allocator.AllocDescriptors(1);
		D3D12_SAMPLER_DESC desc{};
		//Init Sampler Desc From Texture
		GetDevice()->CreateSampler(&desc, descAllocation.CPUHandle());
		m_TextureSamplers.insert(castl::make_pair(samplerDesc, descAllocation));
	}

	CPUDescriptorAllocatorSet::CPUDescriptorAllocatorSet(RenderBackend_D3D12* app) :
		m_SRV_UAV_CBV_Allocator(app, D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		, m_RTV_Allocator(app, D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
		, m_DSV_Allocator(app, D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
	{}


	SamplerManager::SamplerManager(RenderBackend_D3D12* app)
		 : D3D12SubobjectBase(app), m_Sampler_Allocator(app, D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
	{}

}