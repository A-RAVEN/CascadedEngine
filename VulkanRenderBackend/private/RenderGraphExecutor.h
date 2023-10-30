#pragma once
#include "VulkanApplicationSubobjectBase.h"
#include "RenderPassObject.h"
#include "FramebufferObject.h"
#include "VulkanPipelineObject.h"
#include "RenderBackendSettings.h"
#include "ResourceUsageInfo.h"
#include "CVulkanThreadContext.h"
#include "VulkanImageObject.h"
#include <memory>
#include <ThreadManager/header/ThreadManager.h>
#include <RenderInterface/header/CRenderGraph.h>

namespace graphics_backend
{
	using namespace thread_management;
	class RenderGraphExecutor;

	class InternalGPUTextures
	{
		VulkanImageObject m_ImageObject;
		vk::ImageView m_ImageView;
	};

	//RenderPass Executor
	class RenderPassExecutor
	{
	public:
		RenderPassExecutor(RenderGraphExecutor& owningExecutor, CRenderGraph const& renderGraph, CRenderpassBuilder const& renderpassBuilder);
		void Compile(CTaskGraph* taskGraph);

		void ResolveTextureHandleUsages(std::unordered_map<TIndex, ResourceUsage>& TextureHandleUsageStates);

		void PrepareCommandBuffers(CTaskGraph* thisGraph);
		void AppendCommandBuffers(std::vector<vk::CommandBuffer>& outCommandBuffers);
		void SetupFrameBuffer();
	private:
		void CompileRenderPass();
		void CompilePSOs(CTaskGraph* thisGraph);
		void CompilePSOs_SubpassSimpleDrawCall(uint32_t subpassID, CTaskGraph* thisGraph);
		void CompilePSOs_SubpassMeshInterface(uint32_t subpassID, CTaskGraph* thisGraph);

		void ProcessAquireBarriers(vk::CommandBuffer cmd);

		void ExecuteSubpass_SimpleDraw(
			uint32_t subpassID
			, uint32_t width
			, uint32_t height
			, vk::CommandBuffer cmd);

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
		std::vector<std::tuple<TIndex, ResourceUsage, ResourceUsage>> m_UsageBarriers;

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
	private:
		void Compile(CTaskGraph* taskGrap);
		void Execute(CTaskGraph* taskGrap);
		bool m_Compiled = false;

		std::shared_ptr<CRenderGraph> m_RenderGraph = nullptr;
		std::vector<RenderPassExecutor> m_RenderPasses;

		std::unordered_map<TIndex, ResourceUsage> m_TextureHandleUsageStates;
		std::vector<vk::CommandBuffer> m_PendingGraphicsCommandBuffers;

		FrameType m_CompiledFrame = INVALID_FRAMEID;

		std::vector<InternalGPUTextures> m_Images;
	};

	using RenderGraphExecutorDic = HashPool<std::shared_ptr<CRenderGraph>, RenderGraphExecutor>;
}
