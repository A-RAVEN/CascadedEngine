#include "CommandListManager.h"
#include "RenderBackend_D3D12.h"

namespace graphics_backend
{
	CommandListManager::CommandListManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app)
	{
		app->OnDeviceInit([this]()
		{
			GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_DirectAllocator));
			GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&m_BundleAllocator));
			GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_ComputeAllocator));
			GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_CopyAllocator));
		});
	}

	void CommandListManager::Release()
	{
		m_DirectAllocator.Reset();
		m_BundleAllocator.Reset();
		m_ComputeAllocator.Reset();
		m_CopyAllocator.Reset();
	}
	void CommandListManager::Reset()
	{
		m_DirectAllocator->Reset();
		m_BundleAllocator->Reset();
		m_ComputeAllocator->Reset();
		m_CopyAllocator->Reset();
	}
	ID3D12GraphicsCommandList7* CommandListManager::DirectCommand()
	{
		ID3D12GraphicsCommandList7* commandList7 = nullptr;
		GetDevice()->CreateCommandList(0
			, D3D12_COMMAND_LIST_TYPE_DIRECT, m_DirectAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList7));
		return commandList7;
	}
	ID3D12GraphicsCommandList7* CommandListManager::BundleCommand()
	{
		ID3D12GraphicsCommandList7* commandList7 = nullptr;
		GetDevice()->CreateCommandList(0
			, D3D12_COMMAND_LIST_TYPE_BUNDLE, m_BundleAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList7));
		return commandList7;
	}
	ID3D12GraphicsCommandList7* CommandListManager::ComputeCommand()
	{
		ID3D12GraphicsCommandList7* commandList7 = nullptr;
		GetDevice()->CreateCommandList(0
			, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_ComputeAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList7));
		return commandList7;
	}
	ID3D12GraphicsCommandList7* CommandListManager::CopyCommand()
	{
		ID3D12GraphicsCommandList7* commandList7 = nullptr;
		GetDevice()->CreateCommandList(0
			, D3D12_COMMAND_LIST_TYPE_COPY, m_CopyAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList7));
		return commandList7;
	}
}