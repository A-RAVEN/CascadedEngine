#pragma once
#include <CCommandList.h>
#include <GPUBuffer.h>
#include "VulkanIncludes.h"
#include "RenderPassObject.h"
#include "VulkanPipelineObject.h"

namespace graphics_backend
{
	class RenderGraphExecutor;
	class CCommandList_Impl : public CInlineCommandList
	{
	public:
		CCommandList_Impl(vk::CommandBuffer cmd
			, RenderGraphExecutor* renderGraphExecutor
			, castl::shared_ptr<RenderPassObject> renderPassObj
			, TIndex subpassIndex
			, castl::vector<castl::shared_ptr<CPipelineObject>> const& pipelineObjs
		);
		virtual CInlineCommandList& BindPipelineState(uint32_t pipelineStateId) override;
		virtual CInlineCommandList& BindVertexBuffers(castl::vector<GPUBuffer const*> pGPUBuffers, castl::vector<uint32_t> offsets, uint32_t firstBinding) override;
		virtual CInlineCommandList& BindVertexBuffers(castl::vector<GPUBufferHandle> pGPUBuffers, castl::vector<uint32_t> offsets, uint32_t firstBinding) override;
		virtual CInlineCommandList& BindIndexBuffers(EIndexBufferType indexBufferType, GPUBuffer const* pGPUBuffer, uint32_t offset) override;
		virtual CInlineCommandList& BindIndexBuffers(EIndexBufferType indexBufferType, GPUBufferHandle const& gpuBufferHandle, uint32_t offset) override;
		virtual CInlineCommandList& SetShaderBindings(castl::vector<castl::shared_ptr<ShaderBindingSet>> bindings, uint32_t firstBinding) override;
		virtual CInlineCommandList& SetShaderBindings(castl::vector<ShaderBindingSetHandle> bindings, uint32_t firstBinding) override;
		virtual CInlineCommandList& DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t indexOffset = 0, uint32_t vertexOffset = 0) override;
		virtual CInlineCommandList& Draw(uint32_t vertexCount, uint32_t instanceCount = 1) override;
		virtual CInlineCommandList& SetSissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
	private:
		castl::shared_ptr<CPipelineObject> GetBoundPipelineObject() const;
	private:
		vk::CommandBuffer m_CommandBuffer = nullptr;
		castl::shared_ptr<RenderPassObject> m_RenderPassObject = nullptr;
		TIndex m_SubpassIndex = INVALID_INDEX;
		TIndex m_PipelineObjectIndex = INVALID_INDEX;
		castl::vector<castl::shared_ptr<CPipelineObject>> const& m_PipelineObjects;
		RenderGraphExecutor* p_RenderGraphExecutor = nullptr;
	};

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