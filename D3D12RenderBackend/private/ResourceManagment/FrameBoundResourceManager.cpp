#include "FrameBoundResourceManager.h"
#include <RenderBackend_D3D12.h>

namespace graphics_backend
{
	FrameBoundResourceManager::FrameBoundResourceManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app)
		, m_StagingMemoryManager(app) 
		, m_ResourceGPUHeap(app, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		, m_SamplerGPUHeap(app, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
		, m_CommandListManager(app)
		, m_DescriptorAllocatorSet(app)
		, m_AliasedMemoryAllocator(app)
	{
		GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_FrameFences.m_DirectQueueFence));
		GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_FrameFences.m_ComputeQueueFence));
	}

	GPUFrameManager::GPUFrameManager(RenderBackend_D3D12* app, uint64_t maxFrameCount)
		: D3D12SubobjectBase(app), m_MaxFrameContexts(maxFrameCount)
	{
		CA_ASSERT_BREAK(maxFrameCount > 0, "Max Frame Contexts Must Be Greater Than 0");
		app->OnDeviceInit([this]()
		{
			GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_FrameCounterFence));
			GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_ComputeCounterFence));
			m_FrameCounterFence->SetName(L"GlobalDirectFrameCounterFence");
			m_ComputeCounterFence->SetName(L"GlobalComputeFrameCounterFence");
			m_FrameContexts.reserve(m_MaxFrameContexts);
			for (int i = 0; i < m_MaxFrameContexts; ++i)
			{
				m_FrameContexts.emplace_back(GetApp(), m_FrameCounterFence.Get(), m_ComputeCounterFence.Get());
			}
		});
	}

	GPUFrameManager::PFrameContext GPUFrameManager::AquireFrameContext()
	{
		uint64_t currentFrame = m_FrameIndex;
		++m_FrameIndex;
		uint64_t contextID = currentFrame % m_MaxFrameContexts;
		FrameContext& context = m_FrameContexts[contextID];
		context.Aquire(currentFrame);
		return PFrameContext(&context, [this](FrameContext* pContext)
		{
			pContext->Reset();
		});
	}

	void GPUFrameManager::WaitIdle()
	{
		++m_FrameIndex;
		ThrowIfFailed(GetApp()->GetDirectQueue()->Signal(m_FrameCounterFence.Get(), m_FrameIndex));
		ThrowIfFailed(GetApp()->GetComputeQueue()->Signal(m_ComputeCounterFence.Get(), m_FrameIndex));
		if (m_FrameCounterFence->GetCompletedValue() < m_FrameIndex)
		{
			HANDLE eventHandle = CreateEvent(nullptr, false, false, nullptr);
			ThrowIfFailed(m_FrameCounterFence->SetEventOnCompletion(m_FrameIndex, eventHandle));
			DWORD waitResult = WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
			if (waitResult == WAIT_FAILED)
			{
				ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
			}
		}
		if (m_ComputeCounterFence->GetCompletedValue() < m_FrameIndex)
		{
			HANDLE eventHandle = CreateEvent(nullptr, false, false, nullptr);
			ThrowIfFailed(m_ComputeCounterFence->SetEventOnCompletion(m_FrameIndex, eventHandle));
			DWORD waitResult = WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
			if (waitResult == WAIT_FAILED)
			{
				ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
			}
		}
	}

	void GPUFrameManager::Release()
	{
		for (auto& context : m_FrameContexts)
		{
			context.Release();
		}
		m_FrameContexts.clear();
		m_FrameCounterFence.Reset();
		m_ComputeCounterFence.Reset();
	}

}