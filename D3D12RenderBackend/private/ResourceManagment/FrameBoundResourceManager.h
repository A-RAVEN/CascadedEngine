#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <ResourceManagment/MemoryManager.h>
#include <ResourceManagment/CommandListManager.h>
#include <DescriptorManagment/GPUDescriptorHeap.h>
#include <semaphore>
#include <CASTL/CASharedPtr.h>
#include <DebugUtils.h>
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
		void Release() override
		{
			m_DescriptorAllocatorSet.Release();
			m_ResourceGPUHeap.Release();
			m_SamplerGPUHeap.Release();
			m_CommandListManager.Release();
			m_StagingMemoryManager.Release();
		}
		void Reset()
		{
			m_DescriptorAllocatorSet.Reset();
			m_ResourceGPUHeap.Reset();
			m_SamplerGPUHeap.Reset();
			m_CommandListManager.Reset();
			m_StagingMemoryManager.Reset();
		}
	private:
		CPUDescriptorAllocatorSet m_DescriptorAllocatorSet;
		GPUDescriptorHeap m_ResourceGPUHeap;
		GPUDescriptorHeap m_SamplerGPUHeap;
		CommandListManager m_CommandListManager;
		LinearMemoryManager m_StagingMemoryManager;
	};


	class FrameContext : public D3D12SubobjectBase
	{
	public:
		FrameContext(FrameContext&& other): D3D12SubobjectBase(other.GetApp())
			, m_Semaphore(1)
			, m_FrameIndex(std::move(other.m_FrameIndex))
			, m_ResourceManager(std::move(other.m_ResourceManager))
		{
		}
		void Aquire(uint64_t frameID)
		{
			m_Semaphore.acquire();
			m_FrameIndex = frameID;
		}
		void Reset()
		{
			m_ResourceManager.Reset();
			m_Semaphore.release();
		}
		void Release() override
		{
			m_Semaphore.acquire();
			m_ResourceManager.Release();
			m_Semaphore.release();
		}
		FrameContext(RenderBackend_D3D12* app)
			: D3D12SubobjectBase(app)
			, m_Semaphore(1)
			, m_ResourceManager(app)
		{
		}
		FrameBoundResourceManager& GetResourceManager() { return m_ResourceManager; }
	private:
		std::binary_semaphore m_Semaphore;
		uint64_t m_FrameIndex = 0;
		FrameBoundResourceManager m_ResourceManager;
	};

	class GPUFrameManager : public D3D12SubobjectBase
	{
	public:
		using PFrameContext = castl::unique_ptr<FrameContext, castl::function<void(FrameContext*)>>;
		GPUFrameManager(RenderBackend_D3D12* app, uint64_t maxFrameCount = 5);
		PFrameContext AquireFrameContext();
		void Release() override;
	private:
		uint64_t m_FrameIndex = 0;
		uint64_t m_MaxFrameContexts;
		castl::vector<FrameContext> m_FrameContexts;
	};
}