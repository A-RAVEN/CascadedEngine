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

namespace graphics_backend
{
	class PipelineDescData
	{
	public:
		CPipelineStateObject m_PipelineStates;
		CVertexInputDescriptor m_VertexInputDesc;
		GraphicsShaderSet m_ShaderSet;
	};

	class RenderPass
	{
	public:
		RenderPass& SetPipelineState(const CPipelineStateObject& pipelineState);
		RenderPass& SetShaderArguments(const ShaderArgList& shaderArguments);
		RenderPass& SetVertexInputDesc(CVertexInputDescriptor const& vertexInputs);
		RenderPass& SetShaders(GraphicsShaderSet const& shaderBindingList);
		RenderPass& Draw(castl::function<void(CInlineCommandList&)>);
	private:
		class DrawCallBatch
		{
		public:
			//PSO
			PipelineDescData m_PipelineStates;
			//Shader Args
			ShaderArgList m_ShaderArgs;
			//Draw Calls
			castl::vector<castl::function<void(CInlineCommandList&)>> m_DrawCommands;
		};
		PipelineDescData m_CurrentPipelineStates;
		ShaderArgList m_CurrentShaderArgs;
		bool m_StateDirty = true;
		castl::vector<DrawCallBatch> m_DrawCallBatches;

		castl::vector<ImageHandle> m_Arrachments;
		int m_DepthAttachmentIndex = -1;
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
			m_HandleNameToDesc.insert(castl::make_pair( handleName, desc ));
		}
	private:
		castl::unordered_map<castl::string, DescriptorType> m_HandleNameToDesc;
	};

	class GPUGraph
	{
	public:
		//Create a new render pass
		RenderPass& NewRenderPass(ImageHandle const& color);
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
	RenderPass& RenderPass::SetShaderArguments(const ShaderArgList& shaderArguments)
	{
		m_CurrentShaderArgs = shaderArguments;
		m_StateDirty = true;
		return *this;
	}
	RenderPass& RenderPass::SetVertexInputDesc(CVertexInputDescriptor const& vertexInputs)
	{
		m_CurrentPipelineStates.m_VertexInputDesc = vertexInputs;
		m_StateDirty = true;
		return *this;
	}
	RenderPass& RenderPass::SetShaders(GraphicsShaderSet const& shaderBindingList)
	{
		m_CurrentPipelineStates.m_ShaderSet = shaderBindingList;
		m_StateDirty = true;
		return *this;
	}
	RenderPass& RenderPass::Draw(castl::function<void(CInlineCommandList&)> commandFunc)
	{
		if (m_StateDirty)
		{
			m_DrawCallBatches.push_back({ m_CurrentPipelineStates, m_CurrentShaderArgs });
			m_StateDirty = false;
		}
		m_DrawCallBatches.back().m_DrawCommands.push_back(commandFunc);
		return *this;
	}
	RenderPass& GPUGraph::NewRenderPass(ImageHandle const& color)
	{
		m_RenderPasses.emplace_back();
		RenderPass& pass = m_RenderPasses.back();
		pass.m_Arrachments = { color };
		pass.m_DepthAttachmentIndex = -1;
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
		pass.m_DepthAttachmentIndex = -1;
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
	RenderPass& GPUGraph::NewRenderPass(ImageHandle const& color, ImageHandle const& depth)
	{
		m_RenderPasses.emplace_back();
		RenderPass& pass = m_RenderPasses.back();
		pass.m_Arrachments = { color, depth };
		pass.m_DepthAttachmentIndex = -1;
		return pass;
	}
}