#pragma once
#include <Utils/D3D12SubobjectBase.h>

namespace graphics_backend
{
	class CBufferManager : D3D12SubobjectBase
	{
	public:
		CBufferManager(RenderBackend_D3D12* app);
		void Release() override;
		void Reset();
	private:
	};
}