#include "GraphExecutorManager.h"
#include <GPUGraphExecutor/GPUGraphExecutor.h>

namespace graphics_backend
{
	GraphExecutorManager::GraphExecutorManager(CVulkanApplication& app) :
		VKAppSubObjectBaseNoCopy(app)
	{
	}
	GPUGraphExecutor* GraphExecutorManager::NewExecutor(castl::shared_ptr<GPUGraph> const& gpuGraph
		, FrameBoundResourcePool* resourcePool)
	{
		m_Executors.push_back(castl::raii_wrapper<GPUGraphExecutor*>(new GPUGraphExecutor(GetVulkanApplication())
			, [](GPUGraphExecutor* releasedExecutor) {
				delete releasedExecutor;
			}));
		GPUGraphExecutor* result = m_Executors.back().Get();
		result->Initialize(gpuGraph, resourcePool);
		return result;
	}
	void GraphExecutorManager::Release()
	{
		Reset();
	}
	void GraphExecutorManager::Reset()
	{
		for (auto& executor : m_Executors)
		{
			executor->Release();
		}
		m_Executors.clear();
	}
}