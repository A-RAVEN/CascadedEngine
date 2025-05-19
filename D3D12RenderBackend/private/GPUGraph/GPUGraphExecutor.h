#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <unordered_set>
#include <ResourceManagment/MemoryManager.h>
#include "GPUResourceStates.h"
#include "GPUResourceBindingInstance.h"

namespace graphics_backend
{
	class PassBase
	{
	public:
		void CollectShaderStructResourcesForBasePass(D3D2ShaderStruct const& shaderStruct);

		castl::unordered_set<ImageHandle> m_WriteImages;
		castl::unordered_set<ImageHandle> m_ReadImages;
		castl::unordered_set<BufferHandle> m_WriteBuffers;
		castl::unordered_set<BufferHandle> m_ReadBuffers;
	};

	class RasterizationPass : public PassBase
	{
	public:
		void Prepare(RenderPass const& renderPass);
		RenderPass const* pPass;
	};

	class ComputePass : public PassBase
	{
	public:
		void Prepare(ComputeBatch const& computePass);
		ComputeBatch const* pPass;
	};

	class TransferPass : public PassBase
	{
	public:
		void Prepare(GPUDataTransfers const& transferPass);
		GPUDataTransfers const* pPass;
	};


	class GraphNode
	{
	public:
		castl::vector<RasterizationPass*> m_RasterPasses;
		castl::vector<ComputePass*> m_ComputePasses;
		castl::vector<TransferPass*> m_TransferPasses;
	};


	class D3D12GPUGraphExecutor : public D3D12SubobjectBase
	{
	public:
		D3D12GPUGraphExecutor(RenderBackend_D3D12* app);
		void Init(GPUGraph const& owningGraph);

		D3D12GraphLocalResourceManager& GetLocalResourceManager() { return m_LocalResourceManager; }
		GPUConstantBufferManager& GetConstantBufferManager() { return m_ConstantBufferManager; }
		ShaderResourceInstanceDic& GetShaderResourceInstances() { return m_ShaderResourceInstances; }
	private:
		void BuildPassDependencyGraph();

		castl::vector<RasterizationPass> m_RasterizePasses;
		castl::vector<ComputePass> m_ComputePasses;
		castl::vector<TransferPass> m_TransferPasses;

		castl::vector<GraphNode> m_GraphNodes;
		
		D3D12GraphLocalResourceManager m_LocalResourceManager;
		GPUConstantBufferManager m_ConstantBufferManager;
		ShaderResourceInstanceDic m_ShaderResourceInstances;

		CPUDescriptorAllocatorSet m_DescriptorAllocatorSet;
	};
}