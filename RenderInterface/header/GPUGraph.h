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
		castl::vector<castl::function<void(CInlineCommandList&)>> m_DrawCommands;
	};

	class RenderPass
	{
	public:
		RenderPass& SetPipelineState(const CPipelineStateObject& pipelineState);
		RenderPass& SetShaderArguments(castl::shared_ptr<ShaderArgList>& shaderArguments);
		RenderPass& SetInputAssemblyStates(InputAssemblyStates assemblyStates);
		RenderPass& SetVertexInputBinding(castl::string const& bindingName, VertexInputsDescriptor const& attributes);
		RenderPass& SetShaders(IShaderSet const* shaderSet);
		RenderPass& Draw(castl::function<void(CInlineCommandList&)>);

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
		//Create a new render pass
		RenderPass& NewRenderPass(ImageHandle const& color, EAttachmentLoadOp loadOp, EAttachmentStoreOp storeOp, GraphicsClearValue clearValue = {});
		RenderPass& NewRenderPass(ImageHandle const& color, ImageHandle const& depth);
		RenderPass& NewRenderPass(castl::vector<ImageHandle> const& colors, ImageHandle const& depth);
		RenderPass& NewRenderPass(castl::vector<ImageHandle> const& colors);
		//Data Transition
		void ScheduleData(ImageHandle const& imageHandle, void const* data, size_t size);
		void ScheduleData(BufferHandle const& bufferHandle, void const* data, size_t size);
		//Allocate a graph local image
		void AllocImage(ImageHandle const& imageHandle, GPUTextureDescriptor const& desc);
		//Allocate a graph local buffer
		void AllocBuffer(BufferHandle const& bufferHandle, GPUBufferDescriptor const& desc);

		castl::deque<RenderPass> const& GetRenderPasses() const { return m_RenderPasses; }
		GraphResourceManager<GPUTextureDescriptor> const& GetImageManager() const { return m_InternalImageManager; }
		GraphResourceManager<GPUBufferDescriptor> const& GetBufferManager() const { return m_InternalBufferManager; }
	private:
		//Render Passes
		castl::deque<RenderPass> m_RenderPasses;
		//Internal Resources
		GraphResourceManager<GPUTextureDescriptor> m_InternalImageManager;
		GraphResourceManager<GPUBufferDescriptor> m_InternalBufferManager;
	};

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
	RenderPass& RenderPass::SetVertexInputBinding(castl::string const& bindingName, VertexInputsDescriptor const& attributes)
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
	RenderPass& RenderPass::Draw(castl::function<void(CInlineCommandList&)> commandFunc)
	{
		if (m_StateDirty || m_DrawCallBatches.empty())
		{
			m_DrawCallBatches.push_back({ m_CurrentPipelineStates, m_CurrentShaderArgs });
			m_StateDirty = false;
		}
		m_DrawCallBatches.back().m_DrawCommands.push_back(commandFunc);
		return *this;
	}
	RenderPass& GPUGraph::NewRenderPass(ImageHandle const& color, EAttachmentLoadOp loadOp, EAttachmentStoreOp storeOp, GraphicsClearValue clearValue)
	{
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
		m_RenderPasses.emplace_back();
		RenderPass& pass = m_RenderPasses.back();
		pass.m_Arrachments = colors;
		pass.m_Arrachments.push_back(depth);
		pass.m_DepthAttachmentIndex = pass.m_Arrachments.size() - 1;
		return pass;
	}
	RenderPass& GPUGraph::NewRenderPass(castl::vector<ImageHandle> const& colors)
	{
		m_RenderPasses.emplace_back();
		RenderPass& pass = m_RenderPasses.back();
		pass.m_Arrachments = colors;
		pass.m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
		return pass;
	}
	RenderPass& GPUGraph::NewRenderPass(ImageHandle const& color, ImageHandle const& depth)
	{
		m_RenderPasses.emplace_back();
		RenderPass& pass = m_RenderPasses.back();
		pass.m_Arrachments = { color, depth };
		pass.m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
		return pass;
	}
	void GPUGraph::ScheduleData(ImageHandle const& imageHandle, void const* data, size_t size)
	{

	}
	void GPUGraph::ScheduleData(BufferHandle const& bufferHandle, void const* data, size_t size)
	{

	}
	void GPUGraph::AllocImage(ImageHandle const& imageHandle, GPUTextureDescriptor const& desc)
	{
		if (imageHandle.GetType() != ImageHandle::ImageType::Internal)
			return;
		m_InternalImageManager.RegisterHandle(imageHandle.GetName(), desc);
	}
	void GPUGraph::AllocBuffer(BufferHandle const& bufferHandle, GPUBufferDescriptor const& desc)
	{
		if (bufferHandle.GetType() != BufferHandle::BufferType::Internal)
			return;
		m_InternalBufferManager.RegisterHandle(bufferHandle.GetName(), desc);
	}

}