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

}