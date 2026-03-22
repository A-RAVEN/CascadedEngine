#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CASharedPtr.h>
#include "GPUGraph.h"
#include "WindowHandle.h"

namespace graphics_backend
{
	struct GPUFrame
	{
		castl::shared_ptr<GPUGraph> pGraph;
		castl::vector<castl::shared_ptr<WindowHandle>> presentWindows;
	};
}