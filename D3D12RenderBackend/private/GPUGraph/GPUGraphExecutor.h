#pragma once
#include <Utils/D3D12SubobjectBase.h>

namespace graphics_backend
{
	class RasterizationPass
	{

	};

	class ComputePass
	{

	};

	class TransferPass
	{

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