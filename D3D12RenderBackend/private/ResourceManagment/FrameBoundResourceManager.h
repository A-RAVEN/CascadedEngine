#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <ResourceManagment/MemoryManager.h>
#include <ResourceManagment/CommandListManager.h>
#include <DescriptorManagment/GPUDescriptorHeap.h>
#include <semaphore>
namespace graphics_backend
{
	class FrameBoundResourceManager : public D3D12SubobjectBase
	{
	public:
		FrameBoundResourceManager(RenderBackend_D3D12* app);
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
		void Aquire()
		{
			m_Semaphore.acquire();
		}
		void Release()
		{
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
		FrameBoundResourceManager m_ResourceManager;
	};
}