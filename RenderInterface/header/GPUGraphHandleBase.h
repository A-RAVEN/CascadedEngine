#pragma once
#include "Common.h"
namespace graphics_backend
{
	class CRenderGraph;
	class GPUGraphHandleBase
	{
	public:
		GPUGraphHandleBase() = default;
		GPUGraphHandleBase(CRenderGraph* renderGraph, TIndex handleID)
			: p_RenderGraph(renderGraph)
			, m_HandleID(handleID)
		{}
		TIndex GetHandleIndex() const { return m_HandleID; }
		CRenderGraph* GetRenderGraph() const { return p_RenderGraph; }
	protected:
		CRenderGraph* p_RenderGraph = nullptr;
		TIndex m_HandleID = INVALID_INDEX;
	};
}