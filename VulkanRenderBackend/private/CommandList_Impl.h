#pragma once
#include <CCommandList.h>
#include <GPUBuffer.h>
#include "VulkanIncludes.h"
#include "RenderPassObject.h"
#include "VulkanPipelineObject.h"

namespace graphics_backend
{
	class CommandList_Impl : public CommandList
	{
	public:
		CommandList_Impl() = default;
		CommandList_Impl(vk::CommandBuffer cmd) : m_CommandBuffer(cmd) {}
		virtual CommandList& DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t indexOffset = 0, uint32_t vertexOffset = 0, uint32_t firstInstance = 0) override;
		virtual CommandList& Draw(uint32_t vertexCount, uint32_t instanceCount = 1) override;
		virtual CommandList& SetSissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
	private:
		vk::CommandBuffer m_CommandBuffer = nullptr;
	};
}