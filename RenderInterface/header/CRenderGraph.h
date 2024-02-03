#pragma once
#include "TextureHandle.h"
#include "WindowHandle.h"
#include "ShaderBindingSetHandle.h"
#include "CNativeRenderPassInfo.h"
#include "GPUBufferHandle.h"
#include <functional>
#include <string>
#include <vector>
#include <type_traits>

namespace graphics_backend
{
	class CRenderpassBuilder;
	class ShaderConstantSet;
	class TextureSampler;
	class CRenderGraph;
	class GPUBufferDescriptor;

	class CRenderGraph
	{
	public:
		//Used By Client
		//TextureHandle
		virtual TextureHandle NewTextureHandle(GPUTextureDescriptor const& textureDesc) = 0;
		virtual TextureHandle RegisterWindowBackbuffer(WindowHandle* window) = 0;
		virtual GPUTextureDescriptor const& GetTextureDescriptor(TextureHandle const& handle) const = 0;
		//BufferHandle
		virtual GPUBufferHandle NewGPUBufferHandle(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride) = 0;
		virtual void ScheduleBufferData(GPUBufferHandle bufferHandle, uint64_t bufferOffset, uint64_t dataSize, void* pData) = 0;
		virtual GPUBufferDescriptor const& GetGPUBufferDescriptor(TIndex handleID) const = 0;
		//ShaderConstantSetHandle
		virtual ShaderConstantSetHandle NewShaderConstantSetHandle(ShaderConstantsBuilder const& builder) = 0;
		//ShaderBindingSetHandle
		virtual ShaderBindingSetHandle NewShaderBindingSetHandle(ShaderBindingBuilder const& builder) = 0;
		//RenderPass
		virtual CRenderpassBuilder& NewRenderPass(std::vector<CAttachmentInfo> const& inAttachmentInfo) = 0;



		//Used By Backend
		virtual uint32_t GetRenderNodeCount() const = 0;
		virtual CRenderpassBuilder const& GetRenderPass(uint32_t nodeID) const = 0;

		//TextureHandle
		virtual uint32_t GetTextureHandleCount() const = 0;
		virtual uint32_t GetTextureTypesDescriptorCount() const = 0;
		virtual GPUTextureDescriptor const& GetTextureDescriptor(TIndex descriptorIndex) const = 0;
		virtual TIndex WindowHandleToTextureIndex(std::shared_ptr<WindowHandle> handle) const = 0;

		virtual ITextureHandleInternalInfo const& GetGPUTextureInternalData(TIndex handleID) const = 0;
		virtual ITextureHandleInternalInfo& GetGPUTextureInternalData(TIndex handleID) = 0;

		virtual IGPUBufferInternalData const& GetGPUBufferInternalData(TIndex handleID) const = 0;
		virtual IGPUBufferInternalData& GetGPUBufferInternalData(TIndex handleID) = 0;

		//Shader Constants
		virtual IShaderConstantSetData& GetConstantSetData(TIndex constantSetIndex) = 0;
		virtual ShaderConstantsBuilder const& GetShaderConstantDesc(TIndex descID) const = 0;
		virtual uint32_t GetConstantSetCount() const = 0;

		virtual IShaderBindingSetData* GetBindingSetData(TIndex bindingSetIndex) = 0;
		virtual IShaderBindingSetData const* GetBindingSetData(TIndex bindingSetIndex) const = 0;
		virtual uint32_t GetBindingSetDataCount() const = 0;
		virtual ShaderBindingBuilder const& GetShaderBindingSetDesc(TIndex descID) const = 0;

		virtual uint32_t GetGPUBufferHandleCount() const = 0;

		virtual std::unordered_map<WindowHandle*, TIndex> const& WindowHandleToTextureIndexMap() const = 0;

		CRenderpassBuilder& NewRenderPass(
			TextureHandle const& colorAttachment
			, EAttachmentLoadOp colorLoadOp
			, EAttachmentStoreOp colorStoreOp
			, GraphicsClearValue const& clearColor
			, TextureHandle const& depthStencilAttachment
			, EAttachmentLoadOp depthStencilLoadOp
			, EAttachmentStoreOp depthStencilStoreOp
			, GraphicsClearValue const& clearDepthStencil

			, CPipelineStateObject const& pipelineStates
			, CVertexInputDescriptor const& vertexInputs
			, GraphicsShaderSet const& shaderSet
			, ShaderBindingList const& shaderBindingList
			, std::function<void(CInlineCommandList&)> commandFunction)
		{
			CRenderpassBuilder& result = NewRenderPass({ 
				CAttachmentInfo::Make(colorAttachment, colorLoadOp, colorStoreOp, clearColor)
				, CAttachmentInfo::Make(depthStencilAttachment, depthStencilLoadOp, depthStencilStoreOp, clearDepthStencil) });
			result.SetAttachmentTarget(0, colorAttachment);
			result.SetAttachmentTarget(1, depthStencilAttachment);
			result.Subpass({ {0}, 1 }
				, pipelineStates
				, vertexInputs
				, shaderSet
				, shaderBindingList
				, commandFunction);
			return result;
		}

		CRenderpassBuilder& NewRenderPass(
			TextureHandle const& colorAttachment
			, TextureHandle const& depthStencilAttachment

			, CPipelineStateObject const& pipelineStates
			, CVertexInputDescriptor const& vertexInputs
			, GraphicsShaderSet const& shaderSet
			, ShaderBindingList const& shaderBindingList
			, std::function<void(CInlineCommandList&)> commandFunction)
		{
			CRenderpassBuilder& result = NewRenderPass({
				CAttachmentInfo::Make(colorAttachment)
				, CAttachmentInfo::Make(depthStencilAttachment) });
			result.SetAttachmentTarget(0, colorAttachment);
			result.SetAttachmentTarget(1, depthStencilAttachment);
			result.Subpass({ {0}, 1 }
				, pipelineStates
				, vertexInputs
				, shaderSet
				, shaderBindingList
				, commandFunction);
			return result;
		}

		CRenderpassBuilder& NewRenderPass(
			TextureHandle const& colorAttachment
			, TextureHandle const& depthStencilAttachment
			, GraphicsClearValue const& clearDepthStencil

			, CPipelineStateObject const& pipelineStates
			, CVertexInputDescriptor const& vertexInputs
			, GraphicsShaderSet const& shaderSet
			, ShaderBindingList const& shaderBindingList
			, std::function<void(CInlineCommandList&)> commandFunction)
		{
			CRenderpassBuilder& result = NewRenderPass({
				CAttachmentInfo::Make(colorAttachment)
				, CAttachmentInfo::Make(depthStencilAttachment, clearDepthStencil) });
			result.SetAttachmentTarget(0, colorAttachment);
			result.SetAttachmentTarget(1, depthStencilAttachment);
			result.Subpass({ {0}, 1 }
				, pipelineStates
				, vertexInputs
				, shaderSet
				, shaderBindingList
				, commandFunction);
			return result;
		}

		CRenderpassBuilder& NewRenderPass(
			TextureHandle const& colorAttachment
			, GraphicsClearValue const& clearColor
			, TextureHandle const& depthStencilAttachment
			, GraphicsClearValue const& clearDepthStencil

			, CPipelineStateObject const& pipelineStates
			, CVertexInputDescriptor const& vertexInputs
			, GraphicsShaderSet const& shaderSet
			, ShaderBindingList const& shaderBindingList
			, std::function<void(CInlineCommandList&)> commandFunction)
		{
			CRenderpassBuilder& result = NewRenderPass({
				CAttachmentInfo::Make(colorAttachment, clearColor)
				, CAttachmentInfo::Make(depthStencilAttachment, clearDepthStencil) });
			result.SetAttachmentTarget(0, colorAttachment);
			result.SetAttachmentTarget(1, depthStencilAttachment);
			result.Subpass({ {0}, 1 }
				, pipelineStates
				, vertexInputs
				, shaderSet
				, shaderBindingList
				, commandFunction);
			return result;
		}

		CRenderpassBuilder& NewRenderPass(
			TextureHandle const& colorAttachment
			, EAttachmentLoadOp colorLoadOp
			, EAttachmentStoreOp colorStoreOp
			, GraphicsClearValue const& clearColor

			, CPipelineStateObject const& pipelineStates
			, CVertexInputDescriptor const& vertexInputs
			, GraphicsShaderSet const& shaderSet
			, ShaderBindingList const& shaderBindingList
			, std::function<void(CInlineCommandList&)> commandFunction)
		{
			CRenderpassBuilder& result = NewRenderPass({
				CAttachmentInfo::Make(colorAttachment, colorLoadOp, colorStoreOp, clearColor) });
			result.SetAttachmentTarget(0, colorAttachment);
			result.Subpass({ {0} }
				, pipelineStates
				, vertexInputs
				, shaderSet
				, shaderBindingList
				, commandFunction);
			return result;
		}

		CRenderpassBuilder& NewRenderPass(
			TextureHandle const& attachment
			, GraphicsClearValue const& clearValue

			, CPipelineStateObject const& pipelineStates
			, CVertexInputDescriptor const& vertexInputs
			, GraphicsShaderSet const& shaderSet
			, ShaderBindingList const& shaderBindingList
			, std::function<void(CInlineCommandList&)> commandFunction)
		{
			return NewRenderPass(attachment, EAttachmentLoadOp::eClear, EAttachmentStoreOp::eStore, clearValue,
				pipelineStates, vertexInputs, shaderSet, shaderBindingList, commandFunction);
		}

		CRenderpassBuilder& NewRenderPass(
			TextureHandle const& attachment

			, CPipelineStateObject const& pipelineStates
			, CVertexInputDescriptor const& vertexInputs
			, GraphicsShaderSet const& shaderSet
			, ShaderBindingList const& shaderBindingList
			, std::function<void(CInlineCommandList&)> commandFunction)
		{
			return NewRenderPass(attachment, EAttachmentLoadOp::eLoad, EAttachmentStoreOp::eStore, GraphicsClearValue::ClearColor(),
				pipelineStates, vertexInputs, shaderSet, shaderBindingList, commandFunction);
		}
	};
}

#include "ShaderBindingSetHandleInl.h"
#include "GPUBufferHandleInl.h"
#include "TextureHandleInl.h"
