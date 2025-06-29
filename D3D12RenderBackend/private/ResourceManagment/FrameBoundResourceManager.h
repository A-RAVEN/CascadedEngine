#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <ResourceManagment/MemoryManager.h>
#include <semaphore>
namespace graphics_backend
{
	class FrameBoundResourceManager : public D3D12SubobjectBase
	{
	public:
		FrameBoundResourceManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app), m_LocalLinearMemoryManager(app){}
	private:
		LinearMemoryManager m_LocalLinearMemoryManager;
	};


	class FrameContext : public D3D12SubobjectBase
	{
	public:
		std::binary_semaphore m_Semaphore;
		void Aquire()
		{
			m_Semaphore.acquire();
		}
		void Release()
		{
			m_Semaphore.release();
		}
		FrameBoundResourceManager m_ResourceManager;
	};
}