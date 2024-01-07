#pragma once
#include "VulkanIncludes.h"
#include "RenderPassObject.h"
#include "VulkanPipelineObject.h"
#include <RenderInterface/header/CCommandList.h>
#include <RenderInterface/header/GPUBuffer.h>
namespace graphics_backend
{
	class RenderGraphExecutor;
	class CCommandList_Impl : public CInlineCommandList
	{
	public:
		CCommandList_Impl(vk::CommandBuffer cmd
			, RenderGraphExecutor* renderGraphExecutor
			, std::shared_ptr<RenderPassObject> renderPassObj
			, TIndex subpassIndex
			, std::vector<std::shared_ptr<CPipelineObject>> const& pipelineObjs
		);
		virtual CInlineCommandList& BindPipelineState(uint32_t pipelineStateId) override;
		virtual CInlineCommandList& BindVertexBuffers(std::vector<GPUBuffer const*> pGPUBuffers, std::vector<uint32_t> offsets, uint32_t firstBinding) override;
		virtual CInlineCommandList& BindVertexBuffers(std::vector<GPUBufferHandle> pGPUBuffers, std::vector<uint32_t> offsets, uint32_t firstBinding) override;
		virtual CInlineCommandList& BindIndexBuffers(EIndexBufferType indexBufferType, GPUBuffer const* pGPUBuffer, uint32_t offset) override;
		virtual CInlineCommandList& BindIndexBuffers(EIndexBufferType indexBufferType, GPUBufferHandle const& gpuBufferHandle, uint32_t offset) override;
		virtual CInlineCommandList& SetShaderBindings(std::vector<std::shared_ptr<ShaderBindingSet>> bindings, uint32_t firstBinding) override;
		virtual CInlineCommandList& SetShaderBindings(std::vector<ShaderBindingSetHandle> bindings, uint32_t firstBinding) override;
		virtual CInlineCommandList& DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t indexOffset = 0, uint32_t vertexOffset = 0) override;
		virtual CInlineCommandList& Draw(uint32_t vertexCount, uint32_t instanceCount = 1) override;
		virtual CInlineCommandList& SetSissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
	private:
		std::shared_ptr<CPipelineObject> GetBoundPipelineObject() const;
	private:
		vk::CommandBuffer m_CommandBuffer = nullptr;
		std::shared_ptr<RenderPassObject> m_RenderPassObject = nullptr;
		TIndex m_SubpassIndex = INVALID_INDEX;
		TIndex m_PipelineObjectIndex = INVALID_INDEX;
		std::vector<std::shared_ptr<CPipelineObject>> const& m_PipelineObjects;
		RenderGraphExecutor* p_RenderGraphExecutor = nullptr;
	};
}