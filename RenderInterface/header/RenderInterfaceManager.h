#pragma once
#include <CASTL/CASharedPtr.h>
#include "CRenderGraph.h"

namespace graphics_backend
{
	class RenderInterfaceManager
	{
	public:
		virtual castl::shared_ptr<CRenderGraph> NewRenderGraph() = 0;
	};
}