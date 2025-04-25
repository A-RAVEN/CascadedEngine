#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <MemoryManager.h>

namespace graphics_backend
{
	class FrameBoundResourceManager : public D3D12SubobjectBase
	{
	public:
		FrameBoundResourceManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
	private:
		MemoryManager m_FrameLocalMemoryManager;
	};
}