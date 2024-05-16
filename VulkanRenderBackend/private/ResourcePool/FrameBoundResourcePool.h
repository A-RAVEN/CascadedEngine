#pragma once
#include <CASTL/CAVector.h>
#include <GPUResources/GPUResourceInternal.h>

namespace graphics_backend
{
	class BackendFrameCounter
	{

	};

	class ReleasingFrame
	{
	private:
		castl::vector<VKBufferObject> m_ReleasingBuffers;
		castl::vector<VKBufferObject> m_ReleasingImages;
		uint32_t m_ReleasingFrameIndex = 0;
	};
}