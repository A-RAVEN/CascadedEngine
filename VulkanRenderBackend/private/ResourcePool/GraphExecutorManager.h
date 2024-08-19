#pragma once
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAUniquePtr.h>
#include <CASTL/CAVector.h>
#include <CASTL/CARAII.h>
#include <VulkanApplicationSubobjectBase.h>

namespace graphics_backend
{
	class FrameBoundResourcePool;
	class GPUGraphExecutor;
	class GPUGraph;
	class GraphExecutorManager : public VKAppSubObjectBaseNoCopy
	{
	public:
		GraphExecutorManager(CVulkanApplication& app);

		GPUGraphExecutor* NewExecutor(castl::shared_ptr<GPUGraph> const& gpuGraph
			, FrameBoundResourcePool* resourcePool);
		void Release();
		void Reset();
	private:
		castl::vector<castl::raii_wrapper<GPUGraphExecutor*>> m_Executors;
	};
}