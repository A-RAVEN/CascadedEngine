#pragma once
#include <stdint.h>
#include <CASTL/CAVector.h>
#include <CASTL/CASharedPtr.h>
#include "Common.h"
#include "ShaderResourceHandle.h"

namespace graphics_backend
{
	class CommandList
	{
	public:
		virtual CommandList& DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t indexOffset = 0, uint32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;
		virtual CommandList& Draw(uint32_t vertexCount, uint32_t instanceCount = 1) = 0;
		virtual CommandList& SetSissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
	};
}

