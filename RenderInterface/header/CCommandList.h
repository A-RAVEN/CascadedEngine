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
		virtual void BindPipelineState(uint32_t pipelineStateId) = 0;
		virtual void BindVertexBuffers(std::vector<GPUBuffer const*> pGPUBuffers, std::vector<uint32_t> offsets) = 0;
		virtual void BindVertexBuffers(std::vector<GPUBufferHandle> pGPUBuffers, std::vector<uint32_t> offsets) = 0;
		virtual void BindIndexBuffers(EIndexBufferType indexBufferType, GPUBuffer const* pGPUBuffer, uint32_t offset = 0) = 0;
		virtual void BindIndexBuffers(EIndexBufferType indexBufferType, GPUBufferHandle const& gpuBufferHandle, uint32_t offset = 0) = 0;
		virtual void SetShaderBindings(std::vector<std::shared_ptr<ShaderBindingSet>> bindings) = 0;
		virtual void SetShaderBindings(std::vector<ShaderBindingSetHandle> bindings) = 0;
		virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1) = 0;
		virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1) = 0;
		virtual void SetSissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
	};
}

