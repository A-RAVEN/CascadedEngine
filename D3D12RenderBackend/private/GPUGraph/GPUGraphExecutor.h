#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <unordered_set>
#include <ResourceManagment/MemoryManager.h>
#include <ResourceManagment/GPUResourceStates.h>
#include "GPUResourceBindingInstance.h"
#include <ResourceManagment/CommandListManager.h>
#include <ResourceManagment/FrameBoundResourceManager.h>

namespace graphics_backend
{
	class D3D12GPUGraphExecutor : public D3D12SubobjectBase
	{
	public:
		D3D12GPUGraphExecutor(RenderBackend_D3D12* app);
		void CompileAndExecute(GPUGraph const& owningGraph, GPUFrameManager::PFrameContext&& frameContext);
		void Release() override;

		D3D12GraphLocalResourceManager& GetLocalResourceManager() { return m_LocalResourceManager; }
		GPUConstantBufferManager& GetConstantBufferManager() { return m_ConstantBufferManager; }
		ShaderResourceInstanceDic& GetShaderResourceInstances() { return m_ShaderResourceInstances; }
	private:
		void Reset();
		D3D12GraphLocalResourceManager m_LocalResourceManager;
		GPUConstantBufferManager m_ConstantBufferManager;
		ShaderResourceInstanceDic m_ShaderResourceInstances;

		GPUFrameManager::PFrameContext m_CurrentFrameContext;

	};
}