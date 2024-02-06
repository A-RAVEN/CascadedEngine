#pragma once
#include "Common.h"
#include <stdint.h>
#include <CASTL/CAVector.h>
#include <CASTL/CASharedPtr.h>

namespace graphics_backend
{
	class ShaderBindingSet;
	class ShaderBindingSetHandle;
	class GPUBuffer;
	class CInlineCommandList
	{
	public:
		virtual CInlineCommandList& BindPipelineState(uint32_t pipelineStateId) = 0;
		virtual CInlineCommandList& BindVertexBuffers(castl::vector<GPUBuffer const*> pGPUBuffers, castl::vector<uint32_t> byteOffsets = {}, uint32_t firstBinding = 0) = 0;
		virtual CInlineCommandList& BindVertexBuffers(castl::vector<GPUBufferHandle> pGPUBuffers, castl::vector<uint32_t> byteOffsets = {}, uint32_t firstBinding = 0) = 0;
		virtual CInlineCommandList& BindIndexBuffers(EIndexBufferType indexBufferType, GPUBuffer const* pGPUBuffer, uint32_t byteOffset = 0) = 0;
		virtual CInlineCommandList& BindIndexBuffers(EIndexBufferType indexBufferType, GPUBufferHandle const& gpuBufferHandle, uint32_t byteOffset = 0) = 0;
		virtual CInlineCommandList& SetShaderBindings(castl::vector<castl::shared_ptr<ShaderBindingSet>> bindings, uint32_t firstBinding = 0) = 0;
		virtual CInlineCommandList& SetShaderBindings(castl::vector<ShaderBindingSetHandle> bindings, uint32_t firstBinding = 0) = 0;
		virtual CInlineCommandList& DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t indexOffset = 0, uint32_t vertexOffset = 0) = 0;
		virtual CInlineCommandList& Draw(uint32_t vertexCount, uint32_t instanceCount = 1) = 0;
		virtual CInlineCommandList& SetSissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
	};
}

