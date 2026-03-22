#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include "D3D12MemAlloc.h"

namespace graphics_backend
{
	class GPUResource : public D3D12SubobjectBase
	{
	public:
		GPUResource(RenderBackend_D3D12* app);
		GPUResource(D3D12SubobjectBase* parent);
		GPUResource(GPUResource&& other) noexcept;
		GPUResource& operator=(GPUResource&& other) noexcept = default;
		void SetAllocation(D3D12MA::Allocation* allocation);
		void Release() override;
		D3D12MA::Allocation* GetAllocation() const;
		ID3D12Resource* GetResource() const;
	private:
		D3D12MA::Allocation* m_Allocation;
	};
}