#include "CommandListManager.h"
#include "RenderBackend_D3D12.h"

namespace graphics_backend
{
	CommandListManager::CommandListManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app),
		m_DirectAllocator(app, D3D12_COMMAND_LIST_TYPE_DIRECT),
		m_BundleAllocator(app, D3D12_COMMAND_LIST_TYPE_BUNDLE),
		m_ComputeAllocator(app, D3D12_COMMAND_LIST_TYPE_COMPUTE),
		m_CopyAllocator(app, D3D12_COMMAND_LIST_TYPE_COPY)
	{
	}

	void CommandListManager::Release()
	{
		m_DirectAllocator.Release();
		m_BundleAllocator.Release();
		m_ComputeAllocator.Release();
		m_CopyAllocator.Release();
	}
	void CommandListManager::Reset()
	{
		m_DirectAllocator.Reset();
		m_BundleAllocator.Reset();
		m_ComputeAllocator.Reset();
		m_CopyAllocator.Reset();
	}
	ID3D12GraphicsCommandList7* CommandListManager::DirectCommand()
	{
		return m_DirectAllocator.Alloc();
	}
	ID3D12GraphicsCommandList7* CommandListManager::BundleCommand()
	{
		return m_BundleAllocator.Alloc();

	}
	ID3D12GraphicsCommandList7* CommandListManager::ComputeCommand()
	{
		return m_ComputeAllocator.Alloc();

	}
	ID3D12GraphicsCommandList7* CommandListManager::CopyCommand()
	{
		return m_CopyAllocator.Alloc();

	}
	PooledCommandAllocator::PooledCommandAllocator(RenderBackend_D3D12* app, D3D12_COMMAND_LIST_TYPE cmdType)
		: D3D12SubobjectBase(app)
		, m_AllocatorType(cmdType)
	{
		app->OnDeviceInit([this]()
		{
			GetDevice()->CreateCommandAllocator(m_AllocatorType, IID_PPV_ARGS(&m_Allocator));
		});
	}
	ID3D12GraphicsCommandList7* PooledCommandAllocator::Alloc()
	{
		if (m_InUseCount < m_CommandLists.size())
		{
			auto cmd = m_CommandLists[m_InUseCount++];
			cmd->Reset(m_Allocator.Get(), nullptr);
			return cmd;
		}
		else
		{
			ID3D12GraphicsCommandList7* commandList7 = nullptr;
			GetDevice()->CreateCommandList(0
				, m_AllocatorType, m_Allocator.Get(), nullptr, IID_PPV_ARGS(&commandList7));
			m_CommandLists.push_back(commandList7);
			++m_InUseCount;
			return commandList7;
		}
		return nullptr;
	}
	void PooledCommandAllocator::Reset()
	{
		ThrowIfFailed(m_Allocator->Reset());
		m_InUseCount = 0;
	}
	void PooledCommandAllocator::Release()
	{
		ThrowIfFailed(m_Allocator->Reset());
		m_InUseCount = 0;
		for (auto& cmd : m_CommandLists)
		{
			cmd->Release();
		}
		m_CommandLists.clear();
	}
}