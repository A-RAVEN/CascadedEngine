#pragma once
#include <Utils/D3D12SubobjectBase.h>

namespace graphics_backend
{
	class PooledCommandAllocator : D3D12SubobjectBase
	{
	public:
		PooledCommandAllocator(RenderBackend_D3D12* app, D3D12_COMMAND_LIST_TYPE cmdType);
		ID3D12GraphicsCommandList7* Alloc();
		void Reset();
		void Release() override;
	private:
		ComPtr<ID3D12CommandAllocator> m_Allocator;
		uint32_t m_InUseCount = 0;
		castl::vector<ID3D12GraphicsCommandList7*> m_CommandLists;
		D3D12_COMMAND_LIST_TYPE m_AllocatorType;
	};

	class CommandListManager : D3D12SubobjectBase
	{
	public:
		CommandListManager(RenderBackend_D3D12* app);
		void Release() override;
		void Reset();
		ID3D12GraphicsCommandList7* DirectCommand();
		ID3D12GraphicsCommandList7* BundleCommand();
		ID3D12GraphicsCommandList7* ComputeCommand();
		ID3D12GraphicsCommandList7* CopyCommand();
	private:
		PooledCommandAllocator m_DirectAllocator;
		PooledCommandAllocator m_BundleAllocator;
		PooledCommandAllocator m_ComputeAllocator;
		PooledCommandAllocator m_CopyAllocator;
	};
}