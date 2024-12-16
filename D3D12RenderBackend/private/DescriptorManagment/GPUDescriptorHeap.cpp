#include "GPUDescriptorHeap.h"
#include <D3D12Debug.h>
#include <DebugUtils.h>

namespace graphics_backend
{
	void DescriptorHeapAllocator::Init(D3D12_DESCRIPTOR_HEAP_TYPE heapType, bool shaderVisible, uint32_t size)
	{
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
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(
			m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart()
			, offset
			, GetDevice()->GetDescriptorHandleIncrementSize(m_DescriptorHeap->GetDesc().Type));
	}
	CD3DX12_GPU_DESCRIPTOR_HANDLE DescriptorHeapAllocator::GetGPUHandle(uint32_t offset) const
	{
		return CD3DX12_GPU_DESCRIPTOR_HANDLE(
			m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart()
			, offset
			, GetDevice()->GetDescriptorHandleIncrementSize(m_DescriptorHeap->GetDesc().Type));
	}
	void DescriptorHeapAllocator::FreeDescriptors(castl::range<uint32_t> range)
	{
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
}