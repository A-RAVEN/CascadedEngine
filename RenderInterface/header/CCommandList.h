#pragma once
#include "Common.h"
#include <stdint.h>
#include <vector>

namespace graphics_backend
{
	class ShaderBindingSet;
	class ShaderBindingSetHandle;
	class GPUBuffer;
	class CInlineCommandList
	{
	public:
		virtual CInlineCommandList& BindPipelineState(uint32_t pipelineStateId) = 0;
		virtual CInlineCommandList& BindVertexBuffers(std::vector<GPUBuffer const*> pGPUBuffers, std::vector<uint32_t> byteOffsets = {}, uint32_t firstBinding = 0) = 0;
		virtual CInlineCommandList& BindVertexBuffers(std::vector<GPUBufferHandle> pGPUBuffers, std::vector<uint32_t> byteOffsets = {}, uint32_t firstBinding = 0) = 0;
		virtual CInlineCommandList& BindIndexBuffers(EIndexBufferType indexBufferType, GPUBuffer const* pGPUBuffer, uint32_t byteOffset = 0) = 0;
		virtual CInlineCommandList& BindIndexBuffers(EIndexBufferType indexBufferType, GPUBufferHandle const& gpuBufferHandle, uint32_t byteOffset = 0) = 0;
		virtual CInlineCommandList& SetShaderBindings(std::vector<std::shared_ptr<ShaderBindingSet>> bindings) = 0;
		virtual CInlineCommandList& SetShaderBindings(std::vector<ShaderBindingSetHandle> bindings) = 0;
		virtual CInlineCommandList& DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t indexOffset = 0, uint32_t vertexOffset = 0) = 0;
		virtual CInlineCommandList& Draw(uint32_t vertexCount, uint32_t instanceCount = 1) = 0;
		virtual CInlineCommandList& SetSissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
	};
}

