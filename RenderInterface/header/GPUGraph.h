#pragma once
#include "Common.h"
#include "CCommandList.h"
#include "ShaderArgList.h"
#include "CPipelineStateObject.h"
#include "CVertexInputDescriptor.h"
#include "ShaderProvider.h"
#include "ShaderResourceHandle.h"

#include <DebugUtils.h>
#include <CASTL/CAFunctional.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAMap.h>

namespace graphics_backend
{
#pragma region Render Pass And DrawCalls
	class PipelineDescData
	{
	public:
		CPipelineStateObject m_PipelineStates;
		IShaderSet const* m_ShaderSet;
		InputAssemblyStates m_InputAssemblyStates;
		castl::map<castl::string, VertexInputsDescriptor> m_VertexInputBindings;
	};

	class DrawCallBatch
	{
	public:
		//PSO
		PipelineDescData pipelineStateDesc;
		//Shader Args
		castl::shared_ptr<ShaderArgList> shaderArgs;
		//Draw Calls
		castl::vector<castl::function<void(CommandList&)>> m_DrawCommands;

		castl::map<castl::string, BufferHandle> m_BoundVertexBuffers;
		BufferHandle m_BoundIndexBuffer;
		EIndexBufferType m_IndexBufferType = EIndexBufferType::e16;
		uint32_t m_IndexBufferOffset = 0;

		inline DrawCallBatch& SetVertexBuffer(castl::string const& name, BufferHandle const& bufferHandle);
		inline DrawCallBatch& SetIndexBuffer(EIndexBufferType indexBufferType, BufferHandle const& bufferHandle, uint32_t byteOffset);
		inline DrawCallBatch& Draw(castl::function<void(CommandList&)> commandFunc);
	};

	class RenderPass
	{
	public:
		inline RenderPass& SetPipelineState(const CPipelineStateObject& pipelineState);
		inline RenderPass& SetShaderArguments(castl::shared_ptr<ShaderArgList>& shaderArguments);
		inline RenderPass& SetInputAssemblyStates(InputAssemblyStates assemblyStates);
		inline RenderPass& DefineVertexInputBinding(castl::string const& bindingName, VertexInputsDescriptor const& attributes);
		inline RenderPass& SetShaders(IShaderSet const* shaderSet);
		inline DrawCallBatch& DrawCall();

		castl::vector<DrawCallBatch> const& GetDrawCallBatches() const { return m_DrawCallBatches; }
		castl::vector<ImageHandle> const& GetAttachments() const { return m_Arrachments; }
		int GetDepthAttachmentIndex() const { return m_DepthAttachmentIndex; }

		GraphicsClearValue const& GetClearValue() const { return m_ClearValue; }
		EAttachmentLoadOp GetAttachmentLoadOp() const { return m_AttachmentLoadOp; }
		EAttachmentStoreOp GetAttachmentStoreOp() const { return m_AttachmentStoreOp; }
	private:
		PipelineDescData m_CurrentPipelineStates;
		castl::shared_ptr<ShaderArgList> m_CurrentShaderArgs;
		bool m_StateDirty = true;
		EAttachmentLoadOp m_AttachmentLoadOp = EAttachmentLoadOp::eLoad;
		EAttachmentStoreOp m_AttachmentStoreOp = EAttachmentStoreOp::eStore;
		GraphicsClearValue m_ClearValue = {};
		castl::vector<DrawCallBatch> m_DrawCallBatches;
		castl::vector<ImageHandle> m_Arrachments;
		uint32_t m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
		friend class GPUGraph;
	};
#pragma endregion

	struct GPUDataTransfers
	{
		castl::vector<castl::tuple<ImageHandle, void const*, uint64_t, uint64_t>> m_ImageDataUploads;
		castl::vector<castl::tuple<BufferHandle, void const*, uint64_t, uint64_t>> m_BufferDataUploads;
	};

	template <typename DescriptorType>
	class GraphResourceManager
	{
	public:
		void RegisterHandle(castl::string const& handleName, DescriptorType const& desc)
		{
			auto find = m_HandleNameToDesc.find(handleName);
			if (find != m_HandleNameToDesc.end())
			{
				CA_LOG_ERR(castl::string("handleName ") + "aready allocated");
				return;
			};
			m_Descriptors.push_back(desc);
			m_HandleNameToDesc.insert(castl::make_pair( handleName, m_Descriptors.size() - 1));
		}

		int32_t GetDescriptorIndex(castl::string const& handleName) const
		{
			auto find = m_HandleNameToDesc.find(handleName);
			if (find == m_HandleNameToDesc.end())
			{
				CA_LOG_ERR(castl::string("handleName ") + "not found");
				return -1;
			};
			return find->second;
		}

		DescriptorType const* DescriptorIDToDescriptor(int32_t index) const
		{
			if (index < 0 || index >= m_Descriptors.size())
			{
				CA_LOG_ERR(castl::string("index ") + "out of range");
				return nullptr;
			};
			return &m_Descriptors[index];
		}

		DescriptorType const* GetDescriptor(castl::string const& handleName) const
		{
			auto find = m_HandleNameToDesc.find(handleName);
			if (find == m_HandleNameToDesc.end())
			{
				CA_LOG_ERR(castl::string("handleName ") + "not found");
				return nullptr;
			};
			return &m_Descriptors[find->second];
		}
	private:
		castl::unordered_map<castl::string, int32_t> m_HandleNameToDesc;
		castl::vector<DescriptorType> m_Descriptors;
	};

	class GPUGraph
	{
	public:
		enum class EGraphStageType
		{
			eRenderPass,
			eComputePass,
			eTransferPass
		};

		//Create a new render pass
		inline RenderPass& NewRenderPass(ImageHandle const& color, EAttachmentLoadOp loadOp = EAttachmentLoadOp::eClear, EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore, GraphicsClearValue clearValue = {});
		inline RenderPass& NewRenderPass(ImageHandle const& color, ImageHandle const& depth);
		inline RenderPass& NewRenderPass(castl::vector<ImageHandle> const& colors, ImageHandle const& depth);
		inline RenderPass& NewRenderPass(castl::vector<ImageHandle> const& colors);
		//Data Transition
		inline GPUGraph& ScheduleData(ImageHandle const& imageHandle, void const* data, uint64_t size, uint64_t offset = 0);
		inline GPUGraph& ScheduleData(BufferHandle const& bufferHandle, void const* data, uint64_t size, uint64_t offset = 0);
		//Allocate a graph local image
		inline GPUGraph& AllocImage(ImageHandle const& imageHandle, GPUTextureDescriptor const& desc);
		//Allocate a graph local buffer
		inline GPUGraph& AllocBuffer(BufferHandle const& bufferHandle, GPUBufferDescriptor const& desc);

		castl::vector<EGraphStageType> const& GetGraphStages() const { return m_StageTypes; }
		castl::deque<RenderPass> const& GetRenderPasses() const { return m_RenderPasses; }
		castl::vector<GPUDataTransfers> const& GetDataTransfers() const { return m_DataTransfers; }
		castl::vector<uint32_t> const& GetPassIndices() const { return m_PassIndices; }
		GraphResourceManager<GPUTextureDescriptor> const& GetImageManager() const { return m_InternalImageManager; }
		GraphResourceManager<GPUBufferDescriptor> const& GetBufferManager() const { return m_InternalBufferManager; }
	private:
		//Render Passes
		castl::deque<RenderPass> m_RenderPasses;
		//Data Syncs
		castl::vector<GPUDataTransfers> m_DataTransfers;
		//Stages
		castl::vector<EGraphStageType> m_StageTypes;
		//Stage To Pass Index
		castl::vector<uint32_t> m_PassIndices;
		//Internal Resources
		GraphResourceManager<GPUTextureDescriptor> m_InternalImageManager;
		GraphResourceManager<GPUBufferDescriptor> m_InternalBufferManager;
	};

	DrawCallBatch& DrawCallBatch::SetVertexBuffer(castl::string const& name, BufferHandle const& bufferHandle)
	{
		m_BoundVertexBuffers[name] = bufferHandle;
		return *this;
	}
	DrawCallBatch& DrawCallBatch::SetIndexBuffer(EIndexBufferType indexBufferType, BufferHandle const& bufferHandle, uint32_t byteOffset)
	{
		m_BoundIndexBuffer = bufferHandle;
		m_IndexBufferType = indexBufferType;
		m_IndexBufferOffset = byteOffset;
		return *this;
	}
	DrawCallBatch& DrawCallBatch::Draw(castl::function<void(CommandList&)> commandFunc)
	{
		m_DrawCommands.push_back(commandFunc);
		return *this;
	}

	RenderPass& RenderPass::SetPipelineState(const CPipelineStateObject& pipelineState)
	{
		m_CurrentPipelineStates.m_PipelineStates = pipelineState;
		m_StateDirty = true;
		return *this;
	}
	RenderPass& RenderPass::SetInputAssemblyStates(InputAssemblyStates assemblyStates)
	{
		m_CurrentPipelineStates.m_InputAssemblyStates = assemblyStates;
		m_StateDirty = true;
		return *this;
	}

	RenderPass& RenderPass::SetShaderArguments(castl::shared_ptr<ShaderArgList>& shaderArguments)
	{
		m_CurrentShaderArgs = shaderArguments;
		m_StateDirty = true;
		return *this;
	}
	RenderPass& RenderPass::DefineVertexInputBinding(castl::string const& bindingName, VertexInputsDescriptor const& attributes)
	{
		m_CurrentPipelineStates.m_VertexInputBindings[bindingName] = attributes;
		m_StateDirty = true;
		return *this;
	}

	RenderPass& RenderPass::SetShaders(IShaderSet const* shaderSet)
	{
		m_CurrentPipelineStates.m_ShaderSet = shaderSet;
		m_StateDirty = true;
		return *this;
	}
	DrawCallBatch& RenderPass::DrawCall()
	{
		m_DrawCallBatches.push_back({ m_CurrentPipelineStates, m_CurrentShaderArgs });
		return m_DrawCallBatches.back();
	}
	RenderPass& GPUGraph::NewRenderPass(ImageHandle const& color, EAttachmentLoadOp loadOp, EAttachmentStoreOp storeOp, GraphicsClearValue clearValue)
	{
		m_StageTypes.push_back(EGraphStageType::eRenderPass);
		m_PassIndices.push_back(m_RenderPasses.size());
		m_RenderPasses.emplace_back();
		RenderPass& pass = m_RenderPasses.back();
		pass.m_Arrachments = { color };
		pass.m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
		pass.m_AttachmentLoadOp = loadOp;
		pass.m_AttachmentStoreOp = storeOp;
		pass.m_ClearValue = clearValue;
		return pass;
	}
	RenderPass& GPUGraph::NewRenderPass(castl::vector<ImageHandle> const& colors, ImageHandle const& depth)
	{
		m_StageTypes.push_back(EGraphStageType::eRenderPass);
		m_PassIndices.push_back(m_RenderPasses.size());
		m_RenderPasses.emplace_back();
		RenderPass& pass = m_RenderPasses.back();
		pass.m_Arrachments = colors;
		pass.m_Arrachments.push_back(depth);
		pass.m_DepthAttachmentIndex = pass.m_Arrachments.size() - 1;
		return pass;
	}
	RenderPass& GPUGraph::NewRenderPass(castl::vector<ImageHandle> const& colors)
	{
		m_StageTypes.push_back(EGraphStageType::eRenderPass);
		m_PassIndices.push_back(m_RenderPasses.size());
		m_RenderPasses.emplace_back();
		RenderPass& pass = m_RenderPasses.back();
		pass.m_Arrachments = colors;
		pass.m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
		return pass;
	}
	RenderPass& GPUGraph::NewRenderPass(ImageHandle const& color, ImageHandle const& depth)
	{
		m_StageTypes.push_back(EGraphStageType::eRenderPass);
		m_PassIndices.push_back(m_RenderPasses.size());
		m_RenderPasses.emplace_back();
		RenderPass& pass = m_RenderPasses.back();
		pass.m_Arrachments = { color, depth };
		pass.m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
		return pass;
	}
	GPUGraph& GPUGraph::ScheduleData(ImageHandle const& imageHandle, void const* data, uint64_t size, uint64_t offset)
	{
		if (m_StageTypes.empty() || m_StageTypes.back() != EGraphStageType::eTransferPass)
		{
			m_StageTypes.push_back(EGraphStageType::eTransferPass);
			m_PassIndices.push_back(m_DataTransfers.size());
		}
		if (m_DataTransfers.empty())
		{
			m_DataTransfers.emplace_back();
		}
		m_DataTransfers.back().m_ImageDataUploads.push_back({ imageHandle, data, offset, size });
		return *this;
	}
	GPUGraph& GPUGraph::ScheduleData(BufferHandle const& bufferHandle, void const* data, uint64_t size, uint64_t offset)
	{
		if (m_StageTypes.empty() || m_StageTypes.back() != EGraphStageType::eTransferPass)
		{
			m_StageTypes.push_back(EGraphStageType::eTransferPass);
			m_PassIndices.push_back(m_DataTransfers.size());
		}
		if (m_DataTransfers.empty())
		{
			m_DataTransfers.emplace_back();
		}
		m_DataTransfers.back().m_BufferDataUploads.push_back({ bufferHandle, data, offset, size });
		return *this;
	}
	GPUGraph& GPUGraph::AllocImage(ImageHandle const& imageHandle, GPUTextureDescriptor const& desc)
	{
		if (imageHandle.GetType() != ImageHandle::ImageType::Internal)
			return *this;
		m_InternalImageManager.RegisterHandle(imageHandle.GetName(), desc);
		return *this;
	}
	GPUGraph& GPUGraph::AllocBuffer(BufferHandle const& bufferHandle, GPUBufferDescriptor const& desc)
	{
		if (bufferHandle.GetType() != BufferHandle::BufferType::Internal)
			return *this;
		m_InternalBufferManager.RegisterHandle(bufferHandle.GetName(), desc);
		return *this;
	}

}