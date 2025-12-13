#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <ResourceManagment/MemoryManager.h>
#include <ResourceManagment/CommandListManager.h>
#include <DescriptorManagment/GPUDescriptorHeap.h>
#include <semaphore>
#include <CASTL/CASharedPtr.h>
#include <DebugUtils.h>
#include <D3D12Debug.h>
#include <thread>
namespace graphics_backend
{
	struct FrameLocalFences
	{
		ComPtr<ID3D12Fence> m_DirectQueueFence;
		ComPtr<ID3D12Fence> m_ComputeQueueFence;

		void Reset()
		{
			m_DirectQueueFence->Signal(0);
			m_ComputeQueueFence->Signal(0);
		}
		void Release()
		{
			m_DirectQueueFence.Reset();
			m_ComputeQueueFence.Reset();
		}
	};


	class FrameBoundResourceManager : public D3D12SubobjectBase
	{
	public:
		FrameBoundResourceManager(RenderBackend_D3D12* app);
		CPUDescriptorAllocatorSet& GetDescriptorAllocatorSet() { return m_DescriptorAllocatorSet; }
		GPUDescriptorHeap& GetResourceGPUHeap() { return m_ResourceGPUHeap; }
		GPUDescriptorHeap& GetSamplerGPUHeap() { return m_SamplerGPUHeap; }
		CommandListManager& GetCommandListManager() { return m_CommandListManager; }
		LinearMemoryManager& GetStagingMemoryManager() { return m_StagingMemoryManager; }
		AliasedMemoryAllocator& GetAliasedMemoryAllocator() { return m_AliasedMemoryAllocator; }
		FrameLocalFences& GetFrameLocalFences() { return m_FrameFences; }
		void Release() override
		{
			m_DescriptorAllocatorSet.Release();
			m_ResourceGPUHeap.Release();
			m_SamplerGPUHeap.Release();
			m_CommandListManager.Release();
			m_StagingMemoryManager.Release();
			m_AliasedMemoryAllocator.Release();
			m_FrameFences.Release();
		}
		void Reset()
		{
			m_DescriptorAllocatorSet.Reset();
			m_ResourceGPUHeap.Reset();
			m_SamplerGPUHeap.Reset();
			m_CommandListManager.Reset();
			m_StagingMemoryManager.Reset();
			m_AliasedMemoryAllocator.Reset();
			m_FrameFences.Release();
			GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_FrameFences.m_DirectQueueFence));
			GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_FrameFences.m_ComputeQueueFence));

			m_FrameFences.m_DirectQueueFence->SetName(L"FrameLocalDirectQueueFence");
			m_FrameFences.m_ComputeQueueFence->SetName(L"FrameLocalComputeQueueFence");
			//GetDevice()->SetName(m_FrameFences.m_ComputeQueueFence, )
		}
	private:
		CPUDescriptorAllocatorSet m_DescriptorAllocatorSet;
		GPUDescriptorHeap m_ResourceGPUHeap;
		GPUDescriptorHeap m_SamplerGPUHeap;
		CommandListManager m_CommandListManager;
		LinearMemoryManager m_StagingMemoryManager;
		AliasedMemoryAllocator m_AliasedMemoryAllocator;
		FrameLocalFences m_FrameFences;
	};


	class FrameContext : public D3D12SubobjectBase
	{
	public:
		FrameContext(FrameContext&& other) noexcept: D3D12SubobjectBase(other.GetApp())
			, m_Semaphore(1)
			, pFence(std::move(other.pFence))
			, pComputeFence(std::move(other.pComputeFence))
			, m_FenceID(std::move(other.m_FenceID))
			, m_ResourceManager(std::move(other.m_ResourceManager))
		{
		}
		FrameContext(RenderBackend_D3D12* app, ID3D12Fence* fence, ID3D12Fence* computeFence)
			: D3D12SubobjectBase(app)
			, m_Semaphore(1)
			, pFence(fence)
			, pComputeFence(computeFence)
			, m_FenceID(0)
			, m_ResourceManager(app)
		{
		}
		void GPUWaitIdle()
		{
			if (pFence->GetCompletedValue() < m_FenceID)
			{
				HANDLE eventHandle = CreateEvent(nullptr, false, false, nullptr);
				ThrowIfFailed(pFence->SetEventOnCompletion(m_FenceID, eventHandle));
				WaitForSingleObject(eventHandle, INFINITE);
				CloseHandle(eventHandle);
			}
			if (pComputeFence->GetCompletedValue() < m_FenceID)
			{
				HANDLE eventHandle = CreateEvent(nullptr, false, false, nullptr);
				ThrowIfFailed(pComputeFence->SetEventOnCompletion(m_FenceID, eventHandle));
				WaitForSingleObject(eventHandle, INFINITE);
				CloseHandle(eventHandle);
			}
		}
		void Aquire(uint64_t frameID)
		{
			m_Semaphore.acquire();
			GPUWaitIdle();
			m_ResourceManager.Reset();
			m_FenceID = frameID + 1;
		}
		void Reset()
		{
			m_Semaphore.release();
		}
		void Release() override
		{
			m_Semaphore.acquire();
			GPUWaitIdle();
			m_ResourceManager.Release();
			m_Semaphore.release();
		}

		void Signal(ComPtr<ID3D12CommandQueue> const& directQueue
			, ComPtr<ID3D12CommandQueue> const& computeQueue)
		{
			directQueue->Signal(pFence, m_FenceID);
			computeQueue->Signal(pComputeFence, m_FenceID);
		}
		FrameBoundResourceManager& GetResourceManager() { return m_ResourceManager; }
	private:
		std::binary_semaphore m_Semaphore;
		ID3D12Fence* pFence;
		ID3D12Fence* pComputeFence;
		uint64_t m_FenceID;
		FrameBoundResourceManager m_ResourceManager;
	};

	class GPUFrameManager : public D3D12SubobjectBase
	{
	public:
		using PFrameContext = castl::unique_ptr<FrameContext, castl::function<void(FrameContext*)>>;
		GPUFrameManager(RenderBackend_D3D12* app, uint64_t maxFrameCount = 1);
		PFrameContext AquireFrameContext();
		void WaitIdle();
		void Release() override;
	private:
		uint64_t m_FrameIndex = 0;
		uint64_t m_MaxFrameContexts;
		castl::vector<FrameContext> m_FrameContexts;
		ComPtr<ID3D12Fence> m_FrameCounterFence;
		ComPtr<ID3D12Fence> m_ComputeCounterFence;
	};
}