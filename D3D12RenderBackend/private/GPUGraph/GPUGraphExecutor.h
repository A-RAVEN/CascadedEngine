#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <unordered_set>

namespace graphics_backend
{
	class PassBase
	{
	protected:
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
	};

	class ComputePass : public PassBase
	{
	public:
		void Prepare(ComputeBatch const& computePass);
	};

	class TransferPass : public PassBase
	{
	public:
		void Prepare(GPUDataTransfers const& transferPass);
	};


	class GPUPassDependencyGraph
	{
	public:
		class GraphNode
		{
		public:
			castl::vector<RasterizationPass> m_RasterPasses;
			castl::vector<ComputePass> m_ComputePasses;
			castl::vector<TransferPass> m_TransferPasses;
		};
	};


	class D3D12GPUGraphExecutor : public D3D12SubobjectBase
	{
	public:
		D3D12GPUGraphExecutor(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}

	private:
		void BuildPassDependencyGraph();

	};
}