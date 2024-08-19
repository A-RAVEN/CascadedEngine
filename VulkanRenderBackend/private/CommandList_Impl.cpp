#include "pch.h"
#include <ShaderBindingSetHandle.h>
#include "CommandList_Impl.h"
#include "InterfaceTranslator.h"

namespace graphics_backend
{
	CommandList& CommandList_Impl::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t indexOffset, uint32_t vertexOffset, uint32_t firstInstance)
	{
		m_CommandBuffer.drawIndexed(indexCount, instanceCount, indexOffset, vertexOffset, firstInstance);
		return *this;
	}
	CommandList& CommandList_Impl::Draw(uint32_t vertexCount, uint32_t instanceCount)
	{
		m_CommandBuffer.draw(vertexCount, instanceCount, 0, 0);
		return *this;
	}
	CommandList& CommandList_Impl::SetSissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		m_CommandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(x, y), vk::Extent2D(width, height)));
		return *this;
	}
}