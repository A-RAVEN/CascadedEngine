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
		void Release() override
		{
			m_DescriptorAllocatorSet.Release();
			m_ResourceGPUHeap.Release();
			m_SamplerGPUHeap.Release();
			m_CommandListManager.Release();
			m_StagingMemoryManager.Release();
			m_AliasedMemoryAllocator.FreeMemories();
		}
		void Reset()
		{
			m_DescriptorAllocatorSet.Reset();
			m_ResourceGPUHeap.Reset();
			m_SamplerGPUHeap.Reset();
			m_CommandListManager.Reset();
			m_StagingMemoryManager.Reset();
			m_AliasedMemoryAllocator.Release();
		}
	private:
		CPUDescriptorAllocatorSet m_DescriptorAllocatorSet;
		GPUDescriptorHeap m_ResourceGPUHeap;
		GPUDescriptorHeap m_SamplerGPUHeap;
		CommandListManager m_CommandListManager;
		LinearMemoryManager m_StagingMemoryManager;
		AliasedMemoryAllocator m_AliasedMemoryAllocator;
	};


	class FrameContext : public D3D12SubobjectBase
	{
	public:
		FrameContext(FrameContext&& other) noexcept: D3D12SubobjectBase(other.GetApp())
			, m_Semaphore(1)
			, pFence(std::move(other.pFence))
			, m_FenceID(std::move(other.m_FenceID))
			, m_ResourceManager(std::move(other.m_ResourceManager))
		{
		}
		FrameContext(RenderBackend_D3D12* app, ID3D12Fence* fence)
			: D3D12SubobjectBase(app)
			, m_Semaphore(1)
			, pFence(fence)
			, m_FenceID(0)
			, m_ResourceManager(app)
		{
		}
		void GPUWaitIdle()
		{
			//while (pFence->GetCompletedValue() < m_FenceID)
			//{
			//	std::this_thread::yield();
			//}
			if (pFence->GetCompletedValue() < m_FenceID)
			{
				HANDLE eventHandle = CreateEvent(nullptr, false, false, nullptr);
				ThrowIfFailed(pFence->SetEventOnCompletion(m_FenceID, eventHandle));
				WaitForSingleObject(eventHandle, INFINITE);
				CloseHandle(eventHandle);
			}
			CA_LOG("Waiting Fence {} Done, Fence Value {}", m_FenceID, pFence->GetCompletedValue());
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

		ID3D12Fence* GetFence() const { return pFence; }
		void Signal(ComPtr<ID3D12CommandQueue> const& queue)
		{
			CA_LOG("Signal Fence: {}", m_FenceID);
			queue->Signal(pFence, m_FenceID);
		}
		FrameBoundResourceManager& GetResourceManager() { return m_ResourceManager; }
	private:
		std::binary_semaphore m_Semaphore;
		ID3D12Fence* pFence;
		uint64_t m_FenceID;
		FrameBoundResourceManager m_ResourceManager;
	};

	class GPUFrameManager : public D3D12SubobjectBase
	{
	public:
		using PFrameContext = castl::unique_ptr<FrameContext, castl::function<void(FrameContext*)>>;
		GPUFrameManager(RenderBackend_D3D12* app, uint64_t maxFrameCount = 2);
		PFrameContext AquireFrameContext();
		void Release() override;
	private:
		uint64_t m_FrameIndex = 0;
		uint64_t m_MaxFrameContexts;
		castl::vector<FrameContext> m_FrameContexts;
		ComPtr<ID3D12Fence> m_FrameCounterFence;
	};
}