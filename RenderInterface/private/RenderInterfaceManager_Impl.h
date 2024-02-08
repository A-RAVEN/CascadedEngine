#pragma once
#include <RenderInterfaceManager.h>

namespace graphics_backend
{
	class RenderInterfaceManager_Impl : public RenderInterfaceManager
	{
	public:
		virtual castl::shared_ptr<CRenderGraph> NewRenderGraph() override;
	private:
		CRenderGraph* NewRenderGraph_Internal();
		void ReleaseRenderGraph_Internal(CRenderGraph* graph);
	};
}
