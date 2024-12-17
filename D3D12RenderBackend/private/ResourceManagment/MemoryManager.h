#pragma once

#include <Utils/D3D12SubobjectBase.h>
#include <ResourceManagment/GPUResource.h>

namespace graphics_backend
{
	class MemoryManager : public D3D12SubobjectBase
	{
	public:
		MemoryManager(RenderBackend_D3D12* app);
		MemoryManager(MemoryManager&& other) = default;
		MemoryManager& operator=(MemoryManager&& other) = default;
		void Init();
		void Release() override;
		GPUResource AllocGPUResource(D3D12_RESOURCE_DESC const& resourceDesc, D3D12_HEAP_TYPE heapType);
	private:
		ComPtr<D3D12MA::Allocator> m_Allocator;
	};
}