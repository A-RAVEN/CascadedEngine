#pragma once
#include "VulkanApplicationSubobjectBase.h"
#include "RenderPassObject.h"
#include "FramebufferObject.h"
#include "VulkanPipelineObject.h"
#include "RenderBackendSettings.h"
#include "ResourceUsageInfo.h"
#include "CVulkanThreadContext.h"
#include "VulkanImageObject.h"
#include "ShaderDescriptorSetAllocator.h"
#include <memory>
#include <ThreadManager/header/ThreadManager.h>
#include <RenderInterface/header/CRenderGraph.h>

namespace graphics_backend
{
	using namespace thread_management;
	class RenderGraphExecutor;

	class InternalGPUTextures
	{
	public:
		VulkanImageObject m_ImageObject;
		vk::ImageView m_ImageView;
	};

	class TextureHandleLifetimeInfo
	{
	public:
		uint32_t beginNodeID = INVALID_INDEX;
		uint32_t endNodeID = INVALID_INDEX;
		void Touch(uint32_t nodeID)
		{
			if (beginNodeID == INVALID_INDEX)
			{
				beginNodeID = nodeID;
			}
			if (endNodeID == INVALID_INDEX || endNodeID < nodeID)
			{
				endNodeID = nodeID;
			}
		}

		bool Valid() const
		{
			return beginNodeID != INVALID_INDEX;
		}
	};

	class BatchManager : public IBatchManager
	{
	public:
		virtual TIndex RegisterGraphicsPipelineState(GraphicsPipelineStatesData const& pipelineStates) override;
		virtual void AddBatch(std::function<void(CInlineCommandList& commandList)> drawBatchFunc) override;
		std::unordered_map<GraphicsPipelineStatesData, uint32_t, hash_utils::default_hashAlg> const& GetPipelineStates() const { return m_PipelineStates; }
		std::vector<std::function<void(CInlineCommandList& commandList)>> const& GetDrawBatchFuncs() const { return m_DrawBatchFuncs; }
		uint32_t GetPSOCount() const { return m_PipelineStates.size(); }
		GraphicsPipelineStatesData const* GetPSO(uint32_t index) const { return p_PSOs[index]; }
	private:
		std::unordered_map<GraphicsPipelineStatesData, uint32_t, hash_utils::default_hashAlg> m_PipelineStates;
		std::vector<GraphicsPipelineStatesData const*> p_PSOs;
		std::vector<std::function<void(CInlineCommandList& commandList)>> m_DrawBatchFuncs;
	};

	//RenderPass Executor
	class RenderPassExecutor
	{
	public:
		RenderPassExecutor(RenderGraphExecutor& owningExecutor, CRenderGraph const& renderGraph, CRenderpassBuilder const& renderpassBuilder);
		void Compile(CTaskGraph* taskGraph);

		void ResolveTextureHandleUsages(std::unordered_map<TIndex, ResourceUsageFlags>& TextureHandleUsageStates);
		void ResolveBufferHandleUsages(std::unordered_map<TIndex, ResourceUsageFlags>& BufferHandleUsageStates);
		void UpdateTextureLifetimes(uint32_t nodeIndex, std::vector<TextureHandleLifetimeInfo>& textureLifetimes);

		void PrepareCommandBuffers(CTaskGraph* thisGraph);
		void AppendCommandBuffers(std::vector<vk::CommandBuffer>& outCommandBuffers);
		void SetupFrameBuffer();
	private:
		void CompileRenderPass();
		void CompilePSOs(CTaskGraph* thisGraph);
		void CompilePSOs_SubpassSimpleDrawCall(uint32_t subpassID, CTaskGraph* thisGraph);
		void CompilePSOs_SubpassMeshInterface(uint32_t subpassID, CTaskGraph* thisGraph);
		void CompilePSOs_BatchDrawInterface(uint32_t subpassID, CTaskGraph* thisGraph);

		void ProcessAquireBarriers(vk::CommandBuffer cmd);

		void ExecuteSubpass_SimpleDraw(
			uint32_t subpassID
			, uint32_t width
			, uint32_t height
			, vk::CommandBuffer cmd);

		void PrepareBatchDrawInterfaceSecondaryCommands(
			CTaskGraph* thisGraph
			, uint32_t subpassID
			, uint32_t width
			, uint32_t height
			, std::vector<vk::CommandBuffer>& cmdList);


		void PrepareDrawcallInterfaceSecondaryCommands(
			CTaskGraph* thisGraph
			, uint32_t subpassID
			, uint32_t width
			, uint32_t height
			, std::vector<vk::CommandBuffer>& cmdList);

		void ExecuteSubpass_MeshInterface(
			uint32_t subpassID
			, uint32_t width
			, uint32_t height
			, vk::CommandBuffer cmd
			, CVulkanFrameBoundCommandBufferPool& cmdPool);

		RenderGraphExecutor& m_OwningExecutor;
		CRenderpassBuilder const& m_RenderpassBuilder;
		CRenderGraph const& m_RenderGraph;

		//RenderPass and Framebuffer
		std::shared_ptr<RenderPassObject> m_RenderPassObject;
		std::vector<vk::ClearValue> m_ClearValues;


		//Framebuffer
		std::vector<vk::ImageView> m_FrameBufferImageViews;
		std::shared_ptr<FramebufferObject> m_FrameBufferObject;

		//Pipeline States
		std::vector<std::vector<std::shared_ptr<CPipelineObject>>> m_GraphicsPipelineObjects;

		//TextureUsages
		std::vector<std::tuple<TIndex, ResourceUsageFlags, ResourceUsageFlags>> m_ImageUsageBarriers;
		//BufferUsages
		std::vector<std::tuple<TIndex, ResourceUsageFlags, ResourceUsageFlags>> m_BufferUsageBarriers;

		//Batch Manager for drawbatch interfaces
		std::vector<BatchManager> m_BatchManagers;
		//CommandBuffers
		std::vector<vk::CommandBuffer> m_PendingGraphicsCommandBuffers;
		//Secondary CommandBuffers
		std::vector<std::vector<vk::CommandBuffer>> m_PendingSecondaryCommandBuffers;
	};

	//RenderGraph Executor
	class RenderGraphExecutor : public BaseApplicationSubobject
	{
	public:
		RenderGraphExecutor(CVulkanApplication& owner);
		void Create(std::shared_ptr<CRenderGraph> inRenderGraph);
		void Run(CTaskGraph* taskGrap);
		bool CompileDone() const;
		bool CompileIssued() const;
		void CollectCommands(std::vector<vk::CommandBuffer>& inoutCommands) const;
		InternalGPUTextures const& GetLocalTexture(TIndex textureHandle) const;
		VulkanBufferHandle const& GetLocalBuffer(TIndex bufferHandle) const;
		VulkanBufferHandle const& GetLocalConstantSetBuffer(TIndex constantSetHandle) const;
		ShaderDescriptorSetHandle const& GetLocalDescriptorSet(TIndex setHandle) const;
	private:
		void Compile(CTaskGraph* taskGrap);
		void Execute(CTaskGraph* taskGrap);
		void AllocateGPUBuffers(CTaskGraph* taskGrap);
		void AllocateShaderBindingSets(CTaskGraph* taskGrap);
		void WriteShaderBindingSets(CTaskGraph* taskGrap);
		bool m_Compiled = false;

		std::shared_ptr<CRenderGraph> m_RenderGraph = nullptr;
		std::vector<RenderPassExecutor> m_RenderPasses;

		std::unordered_map<TIndex, ResourceUsageFlags> m_TextureHandleUsageStates;
		std::unordered_map<TIndex, ResourceUsageFlags> m_BufferHandleUsageStates;

		std::vector<vk::CommandBuffer> m_PendingGraphicsCommandBuffers;

		FrameType m_CompiledFrame = INVALID_FRAMEID;

		std::vector<int32_t> m_TextureAllocationIndex;
		std::vector<std::vector<InternalGPUTextures>> m_Images;

		//Internal GPU Buffers
		std::vector<VulkanBufferHandle> m_GPUBufferObjects;
		uint32_t m_GPUBufferOffset = 0;
		uint32_t m_ConstantBufferOffset = 0;
		//Internal DescriptorSets
		std::vector<ShaderDescriptorSetHandle> m_DescriptorSets;
	};

	using RenderGraphExecutorDic = HashPool<std::shared_ptr<CRenderGraph>, RenderGraphExecutor>;
}
