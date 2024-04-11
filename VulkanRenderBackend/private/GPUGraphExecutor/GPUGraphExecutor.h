#pragma once
#include <GPUGraph.h>
#include <VulkanApplicationSubobjectBase.h>
namespace graphics_backend
{
	class GPUGraphExecutor : VKAppSubObjectBase
	{
	public:
		void PrepareGraph();
	private:
		void PrepareFrameBufferAndPSOs();
	private:
		GPUGraph m_Graph;
	};
}