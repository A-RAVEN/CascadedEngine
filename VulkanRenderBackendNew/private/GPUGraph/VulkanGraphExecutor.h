#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <GPUGraph/VulkanResourceState.h>
#include <GPUGraph/VulkanGraphLocalResourceManager.h>
#include <GPUGraph/VulkanPassRWState.h>
#include <GPUGraph/VulkanResourceBindingInstance.h>
#include <GPUGraph/VulkanConstantBufferManager.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CASet.h>
#include <CASTL/CADeque.h>
#include <GPUGraph.h>
#include <CATimer/Timer.h>
#include <ResourceManagement/VulkanFrameManager.h>

namespace graphics_backend
{
	// Forward declarations
	class VulkanBuffer;
	class VulkanTexture;
	class VulkanShaderStruct;

	// Get descriptor from image handle
	GPUTextureDescriptor GetDescriptor(GPUGraph const& graph, ImageHandle const& image);
	GPUBufferDescriptor GetDescriptor(GPUGraph const& graph, BufferHandle const& buffer);

	// Per-pass read/write state tracking (Vulkan executor-specific)
	class VulkanExecutorRWState
	{
	public:
		castl::unordered_map<ImageHandle, VulkanResourceState> imageRWStates;
		castl::unordered_map<BufferHandle, VulkanResourceState> bufferRWStates;
		castl::unordered_map<VulkanShaderStruct const*, EGPUQueueTypeFlags> cBufferUsageStates;
		EGPUQueueTypeFlags batchResourceQueueTypes = EGPUQueueTypeFlags{};

		bool Depends(VulkanExecutorRWState const& successor) const;
		void Append(VulkanExecutorRWState const& other);

		void SetImageRWState(ImageHandle const& image, vk::PipelineStageFlags stages,
			vk::AccessFlags access, vk::ImageLayout layout, EGPUQueueType queueType);
		void SetBufferRWState(BufferHandle const& buffer, vk::PipelineStageFlags stages,
			vk::AccessFlags access, EGPUQueueType queueType);
		void SetCBufferUsageState(VulkanShaderStruct const* pCBufferStruct,
			vk::PipelineStageFlags stages, EGPUQueueType queueType);
	};

	// Dependency tracking
	class VulkanPassDependency
	{
	public:
		VulkanExecutorRWState const& rwState;
		uint32_t passID;
		GPUGraph::EGraphStageType passType;
		uint32_t predecessorCount = 0;
		castl::vector<VulkanPassDependency*> successors;

		VulkanPassDependency(VulkanExecutorRWState const& rwState, uint32_t passID, GPUGraph::EGraphStageType passType)
			: rwState(rwState), passID(passID), passType(passType) {}

		bool DepsFree() const { return predecessorCount == 0; }
		void RemoveSelfDeps();
		void CheckAddSuccessor(VulkanPassDependency* successor);
	};

	// Resource usage range tracking
	struct VulkanResourceUsageRangeData
	{
		struct BatchAndState
		{
			uint32_t batchID;
			VulkanResourceState state;
		};
		castl::vector<BatchAndState> states;

		void Expand(uint32_t batchID, VulkanResourceState const& state) {
			states.push_back({ batchID, state });
		}
	};

	struct VulkanCBufferUsageData
	{
		castl::set<uint32_t> lifeTime;
		EGPUQueueTypeFlags queueTypes = EGPUQueueTypeFlags{};

		void Encapsule(uint32_t batchID, EGPUQueueTypeFlags flags) {
			lifeTime.insert(batchID);
			queueTypes |= flags;
		}
	};

	// Render pass GPU data
	struct VulkanRenderPassGPUData
	{
		struct DrawCallGPUData
		{
			vk::Viewport defaultViewport;
			vk::Rect2D defaultScissor;
			castl::vector<castl::pair<VertexInputsDescriptor, BufferHandle>> inputAssemblyBindingBuffers;
		};

		struct DrawCallBatchGPUData
		{
			vk::PrimitiveTopology topology;
			vk::Pipeline pipeline;
			vk::PipelineLayout pipelineLayout;
			VulkanResourceBindingInstance const* pResourceBindingInstance = nullptr;
			castl::vector<DrawCallGPUData> drawcalls;
		};

		castl::vector<DrawCallBatchGPUData> drawcallBatchs;
	};

	// Compute pass GPU data
	struct VulkanComputePassGPUData
	{
		struct DispatchGPUData
		{
			vk::Pipeline pipeline;
			vk::PipelineLayout pipelineLayout;
			VulkanResourceBindingInstance const* pResourceBindingInstance = nullptr;
		};
		castl::vector<DispatchGPUData> dispatchs;
	};

	// Image barrier info
	struct VulkanImageBarrier
	{
		ImageHandle handle;
		vk::ImageMemoryBarrier barrier;
	};

	// Buffer barrier info
	struct VulkanBufferBarrier
	{
		BufferHandle handle;
		vk::BufferMemoryBarrier barrier;
	};

	// Resource barriers for a batch
	struct VulkanRenderStateBarriers
	{
		castl::vector<VulkanImageBarrier> imageBarriers;
		castl::vector<VulkanBufferBarrier> bufferBarriers;

		void AddImageBarrier(ImageHandle const& handle, vk::ImageMemoryBarrier const& barrier);
		void AddBufferBarrier(BufferHandle const& handle, vk::BufferMemoryBarrier const& barrier);
		bool IsEmpty() const { return imageBarriers.empty() && bufferBarriers.empty(); }
		bool AnyBarrier() const { return !IsEmpty(); }
		void ExecuteBarriers(vk::CommandBuffer cmdBuf);
	};

	// CBuffer initialization barriers
	struct VulkanCBufferInitializeBarriers
	{
		castl::vector<castl::pair<uint64_t, VulkanShaderStruct const*>> cbufferData;

		void AddCBuffer(uint64_t resourceId, VulkanShaderStruct const* shaderStruct);
		bool AnyBarrier() const { return !cbufferData.empty(); }
	};

	// Execution batch
	struct VulkanGPUExecutionBatch
	{
		castl::vector<uint32_t> rasterPassRefs;
		castl::vector<uint32_t> computePassRefs;
		castl::vector<uint32_t> transferPassRefs;
		bool hasFinalizePass = false;

		VulkanRenderStateBarriers aquireBarriers;
		VulkanRenderStateBarriers releaseBarriers;
		VulkanRenderStateBarriers computeAquireBarriers;
		VulkanRenderStateBarriers computeReleaseBarriers;

		VulkanCBufferInitializeBarriers cbufferBarriers;
		VulkanCBufferInitializeBarriers computeCBufferBarriers;

		VulkanExecutorRWState batchRWStates;
		bool anyComputeQueueOperations = false;

		VulkanRenderStateBarriers& GetAquireBarriers(EGPUQueueTypeFlags queueFlags);
		VulkanRenderStateBarriers& GetReleaseBarriers(EGPUQueueTypeFlags queueFlags);
		VulkanCBufferInitializeBarriers& GetCBufferBarriers(EGPUQueueTypeFlags queueFlags);

		vk::CommandBuffer directCommandBuffer;
		vk::CommandBuffer computeCommandBuffer;
	};

	// Shader resource set for binding lookup
	class VulkanShaderResourceSet
	{
	public:
		ShaderInfo shaderInfo;
		castl::vector<ShaderStructDic const*> shaderStructs;
		castl::unordered_map<cacore::NameHash, VulkanShaderStruct const*> resourceDic;
		size_t hash = 0;

		void Init(RenderBackend_Vulkan* pApp, ShaderInfo const& info,
			castl::vector<ShaderStructDic const*> const& structs);

		bool operator==(VulkanShaderResourceSet const& other) const {
			return hash == other.hash;
		}
	};

	// Main graph executor
	class VulkanGraphExecutor : public VulkanSubobjectBase
	{
	public:
		VulkanGraphExecutor() = default;
		~VulkanGraphExecutor() = default;

		virtual void Release() override;

		// Main entry point (Task 3.1: new signature with PFrameContext&&)
		void CompileAndExecute(castl::shared_ptr<GPUGraph> const& graph,
			VulkanGPUFrameManager::PFrameContext&& frameContext);

		// Resource manager access (public for VulkanResourceBindingInstance)
		VulkanGraphLocalResourceManager& GetLocalResourceManager() { return m_LocalResourceManager; }

	private:
		// Phase 1: Prepare
		void Prepare(GPUGraph const& graph);
		void InitArraySizes(GPUGraph const& graph);
		void CollectResources(GPUGraph const& graph);
		void CollectShaderBindings(GPUGraph const& graph);
		void RegisterComputeResources(GPUGraph const& graph);
		void RegisterCBufferUsageStates(GPUGraph const& graph);
		void RegisterCBufferForAliasing(GPUGraph const& graph);

		// Phase 2: Build dependency-free batches
		void BuildDependencyFreeBatches(GPUGraph const& graph);

		// Phase 3: Build resource usage ranges
		void BuildResourceUsageRanges();

		// Phase 4: Allocate aliased resources
		void AllocateAliasedResources();

		// Phase 5: Prepare batch resource barriers
		void PrepareBatchResourceBarriers(GPUGraph const& graph);

		// Phase 6: Build pipeline states
		void BuildPipelineStates(GPUGraph const& graph);

		// Phase 7: Execute
		void Execute(GPUGraph const& graph);

		// QFOT helper: map EGPUQueueType to Vulkan queue family index
		int GetQueueFamilyIndex(EGPUQueueType queueType) const;

		// Helpers
		void RecordBatchCommands(VulkanGPUExecutionBatch& batch, GPUGraph const& graph);
		void RecordRenderPass(VulkanGPUExecutionBatch& batch, uint32_t rasterPassID, GPUGraph const& graph);
		void RecordComputePass(VulkanGPUExecutionBatch& batch, uint32_t computePassID, GPUGraph const& graph, bool asyncCompute);
		void RecordTransferPass(VulkanGPUExecutionBatch& batch, uint32_t transferPassID, GPUGraph const& graph);
		void SubmitBatches(GPUGraph const& graph);
		void ApplyExternalResourceStates();
		void PresentWindows(GPUGraph const& graph);
		void Reset();

		// Resource managers
		VulkanGraphLocalResourceManager m_LocalResourceManager;
		VulkanConstantBufferManager m_ConstantBufferManager;

		// Per-pass states
		castl::vector<VulkanExecutorRWState> m_RasterPassRWStates;
		castl::vector<VulkanExecutorRWState> m_ComputePassRWStates;
		castl::vector<VulkanExecutorRWState> m_TransferPassRWStates;
		VulkanExecutorRWState m_FinalizePassRWState;

		// GPU data per pass
		castl::vector<VulkanRenderPassGPUData> m_RasterPassGPUData;
		castl::vector<VulkanComputePassGPUData> m_ComputePassGPUData;

		// Execution batches
		castl::vector<VulkanGPUExecutionBatch> m_ExecutionBatches;

		// Resource lifetimes
		castl::unordered_map<ImageHandle, VulkanResourceUsageRangeData> m_ImageLifetimes;
		castl::unordered_map<BufferHandle, VulkanResourceUsageRangeData> m_BufferLifetimes;
		castl::unordered_map<VulkanShaderStruct const*, VulkanCBufferUsageData> m_CBufferLifetimes;

		// Shader resource instances
		castl::unordered_map<size_t, castl::shared_ptr<VulkanResourceBindingInstance>> m_ShaderResourceInstances;

		// Current graph
		castl::shared_ptr<GPUGraph> m_CurrentGraph;

		// Timing
		uint64_t m_PrepareTime = 0;
		uint64_t m_ExecuteTime = 0;

		// Cached queue family indices for QFOT
		int m_GraphicsQueueFamily = static_cast<int>(vk::QueueFamilyIgnored);
		int m_ComputeQueueFamily = static_cast<int>(vk::QueueFamilyIgnored);

		// Current frame context (Task 3.1: per-frame resource context)
		VulkanGPUFrameManager::PFrameContext m_CurrentFrameContext;
	};
}
