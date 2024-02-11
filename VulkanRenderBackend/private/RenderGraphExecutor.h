#pragma once
#include <ThreadManager.h>
#include <CRenderGraph.h>
#include "VulkanApplicationSubobjectBase.h"
#include "RenderPassObject.h"
#include "FramebufferObject.h"
#include "VulkanPipelineObject.h"
#include "RenderBackendSettings.h"
#include "ResourceUsageInfo.h"
#include "CVulkanThreadContext.h"
#include "VulkanImageObject.h"
#include "ShaderDescriptorSetAllocator.h"

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
		virtual void AddBatch(castl::function<void(CInlineCommandList& commandList)> drawBatchFunc) override;
		castl::unordered_map<GraphicsPipelineStatesData, uint32_t, hash_utils::default_hashAlg> const& GetPipelineStates() const { return m_PipelineStates; }
		castl::vector<castl::function<void(CInlineCommandList& commandList)>> const& GetDrawBatchFuncs() const { return m_DrawBatchFuncs; }
		uint32_t GetPSOCount() const { return m_PipelineStates.size(); }
		GraphicsPipelineStatesData const* GetPSO(uint32_t index) const { return p_PSOs[index]; }
	private:
		castl::unordered_map<GraphicsPipelineStatesData, uint32_t, hash_utils::default_hashAlg> m_PipelineStates;
		castl::vector<GraphicsPipelineStatesData const*> p_PSOs;
		castl::vector<castl::function<void(CInlineCommandList& commandList)>> m_DrawBatchFuncs;
	};

	//RenderPass Executor
	class RenderPassExecutor
	{
	public:
		RenderPassExecutor(
			RenderGraphExecutor& owningExecutor
			, CRenderGraph const& renderGraph
			, CRenderpassBuilder const& renderpassBuilder);
		RenderPassExecutor(RenderPassExecutor const& other) = default;
		void Compile(CTaskGraph* taskGraph);

		void ResolveTextureHandleUsages(castl::unordered_map<TIndex, ResourceUsageFlags>& TextureHandleUsageStates);
		void ResolveBufferHandleUsages(castl::unordered_map<TIndex, ResourceUsageFlags>& BufferHandleUsageStates);
		void UpdateTextureLifetimes(uint32_t nodeIndex, castl::vector<TextureHandleLifetimeInfo>& textureLifetimes);

		void PrepareCommandBuffers(CTaskGraph* thisGraph);
		void AppendCommandBuffers(castl::vector<vk::CommandBuffer>& outCommandBuffers);
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
			, castl::vector<vk::CommandBuffer>& cmdList);


		void PrepareDrawcallInterfaceSecondaryCommands(
			CTaskGraph* thisGraph
			, uint32_t subpassID
			, uint32_t width
			, uint32_t height
			, castl::vector<vk::CommandBuffer>& cmdList);

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
		castl::shared_ptr<RenderPassObject> m_RenderPassObject;
		castl::vector<vk::ClearValue> m_ClearValues;

		//Framebuffer
		castl::vector<vk::ImageView> m_FrameBufferImageViews;
		castl::shared_ptr<FramebufferObject> m_FrameBufferObject;

		//Pipeline States
		castl::vector<castl::vector<castl::shared_ptr<CPipelineObject>>> m_GraphicsPipelineObjects;

		//TextureUsages
		castl::vector<castl::tuple<TIndex, ResourceUsageFlags, ResourceUsageFlags>> m_ImageUsageBarriers;
		//BufferUsages
		castl::vector<castl::tuple<TIndex, ResourceUsageFlags, ResourceUsageFlags>> m_BufferUsageBarriers;

		//Batch Manager for drawbatch interfaces
		castl::vector<BatchManager> m_BatchManagers;
		//CommandBuffers
		castl::vector<vk::CommandBuffer> m_PendingGraphicsCommandBuffers;
		//Secondary CommandBuffers
		castl::vector<castl::vector<vk::CommandBuffer>> m_PendingSecondaryCommandBuffers;
	};

	//RenderGraph Executor
	class RenderGraphExecutor : public VKAppSubObjectBase
	{
	public:
		RenderGraphExecutor(CVulkanApplication& owner, FrameType frameID);
		void Create(castl::shared_ptr<CRenderGraph> inRenderGraph);
		void Run(CTaskGraph* taskGrap);
		bool CompileDone() const;
		bool CompileIssued() const;
		void CollectCommands(castl::vector<vk::CommandBuffer>& inoutCommands) const;
		FrameType GetCurrentFrameID() const {
			return m_CurrentFrameID;
		}
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

		castl::shared_ptr<CRenderGraph> m_RenderGraph = nullptr;
		castl::vector<RenderPassExecutor> m_RenderPasses;

		castl::unordered_map<TIndex, ResourceUsageFlags> m_TextureHandleUsageStates;
		castl::unordered_map<TIndex, ResourceUsageFlags> m_BufferHandleUsageStates;

		castl::vector<vk::CommandBuffer> m_PendingGraphicsCommandBuffers;

		FrameType m_CompiledFrame = INVALID_FRAMEID;
		FrameType m_CurrentFrameID;

		castl::vector<int32_t> m_TextureAllocationIndex;
		castl::vector<castl::vector<InternalGPUTextures>> m_Images;

		//Internal GPU Buffers
		castl::vector<VulkanBufferHandle> m_GPUBufferObjects;
		uint32_t m_GPUBufferOffset = 0;
		uint32_t m_ConstantBufferOffset = 0;
		//Internal DescriptorSets
		castl::vector<ShaderDescriptorSetHandle> m_DescriptorSets;
	};

	using RenderGraphExecutorDic = HashPool<castl::shared_ptr<CRenderGraph>, RenderGraphExecutor>;
}
