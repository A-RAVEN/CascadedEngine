#include "FrameBoundResourceManager.h"

namespace graphics_backend
{
	FrameBoundResourceManager::FrameBoundResourceManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app)
		, m_StagingMemoryManager(app) 
		, m_ResourceGPUHeap(app, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		, m_SamplerGPUHeap(app, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
		, m_CommandListManager(app)
		, m_DescriptorAllocatorSet(app)
	{}

}