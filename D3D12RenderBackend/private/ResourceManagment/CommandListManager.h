#pragma once
#include <Utils/D3D12SubobjectBase.h>

namespace graphics_backend
{
	class CommandListManager : D3D12SubobjectBase
	{
	public:
		CommandListManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		void DeviceInit() override;
		void Release() override;
		void Reset();
		ID3D12GraphicsCommandList7* DirectCommand();
		ID3D12GraphicsCommandList7* BundleCommand();
		ID3D12GraphicsCommandList7* ComputeCommand();
		ID3D12GraphicsCommandList7* CopyCommand();
	private:
		ComPtr<ID3D12CommandAllocator> m_DirectAllocator;
		ComPtr<ID3D12CommandAllocator> m_BundleAllocator;
		ComPtr<ID3D12CommandAllocator> m_ComputeAllocator;
		ComPtr<ID3D12CommandAllocator> m_CopyAllocator;
	};
}