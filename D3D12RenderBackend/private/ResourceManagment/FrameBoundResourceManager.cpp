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

	GPUFrameManager::GPUFrameManager(RenderBackend_D3D12* app, uint64_t maxFrameCount)
		: D3D12SubobjectBase(app), m_MaxFrameContexts(maxFrameCount)
	{
		CA_ASSERT_BREAK(maxFrameCount > 0, "Max Frame Contexts Must Be Greater Than 0");
		m_FrameContexts.reserve(m_MaxFrameContexts);
		for (int i = 0; i < m_MaxFrameContexts; ++i)
		{
			m_FrameContexts.emplace_back(app);
		}
	}

	GPUFrameManager::PFrameContext GPUFrameManager::AquireFrameContext()
	{
		uint64_t currentFrame = m_FrameIndex;
		++m_FrameIndex;
		uint64_t contextID = currentFrame % m_MaxFrameContexts;
		FrameContext& context = m_FrameContexts[contextID];
		context.Aquire(contextID);
		return PFrameContext(&context, [&](FrameContext* pContext)
		{
			pContext->Reset();
		});
	}

	void GPUFrameManager::Release()
	{
		for (auto& context : m_FrameContexts)
		{
			context.Release();
		}
		m_FrameContexts.clear();
	}

}