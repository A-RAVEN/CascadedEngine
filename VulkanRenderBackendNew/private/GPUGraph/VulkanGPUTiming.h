#pragma once
// vulkan-gpu-queries（能力 `gpu-graph-timing`）任务 3.1：Vulkan 的逐 pass GPU 计时结果对象。
//
// 设计要点（design D4/D5/D7/D10）：
//  - 按图实际使用的**命令缓冲区**分别创建 vk::QueryPool 与宿主可见回读缓冲；未使用的不创建。
//    在 compute 命令缓冲区上记录的 pass 用 compute 池，其余（渲染 / 非异步计算）用 direct 池。
//  - vkCmdResetQueryPool 在写时间戳之前、render pass 外记录一次（每条池只重置一次）。
//  - 复制用 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT，位置在 endRenderPass() 之后、
//    命令缓冲区 end() 之前；回读缓冲带 VK_BUFFER_USAGE_TRANSFER_DST_BIT，64 位结果的偏移与步长
//    按 8 字节对齐。
//  - 结果对象自己持有查询池 / 回读缓冲 / CPU 数组；不引用执行器，也不指向会被 Reset() 复用的帧缓冲。
//  - 公开只有纳秒；timestampValidBits 不足 64 位时按掩码处理，换算后 endNs < beginNs 就整次失败。
//
// 实现放在已有的 `VulkanGraphExecutor.cpp`（本 change 不新增 .cpp，理由见 `CRenderBackend.h`）。

#include <CRenderBackend.h>
#include <GPUGraph.h>
#include <Utils/VulkanSubobjectBase.h>
#include <Utils/VulkanIncludes.h>
#include <vk_mem_alloc.h>
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>

namespace graphics_backend
{
	class RenderBackend_Vulkan;

	class VulkanGPUTiming : public IGPUGraphTiming, public VulkanSubobjectBase
	{
	public:
		~VulkanGPUTiming() override;

		// 按图实际使用的命令缓冲区分配查询池与回读缓冲。返回 nullptr = 队列不支持时间戳 / 分配失败
		// （原因已用 CA_LOG_ERR 记录）。
		static std::unique_ptr<VulkanGPUTiming> Create(
			RenderBackend_Vulkan* pBackend,
			GPUGraph const& graph,
			uint64_t submissionID);

		// ---- 记录（由 VulkanGraphExecutor 在命令缓冲区上调用，顺序 = 提交顺序）----
		// computeCommandBuffer 决定用哪个池；reportedQueue 是该 pass 结果里的"实际队列"字段。
		void RecordPassBegin(vk::CommandBuffer cmdBuf, bool computeCommandBuffer,
			EGPUTimedPassType passType, uint32_t passIndex, castl::string const& name,
			EGPUQueueType reportedQueue);
		void RecordPassEnd(vk::CommandBuffer cmdBuf, bool computeCommandBuffer);
		// 命令缓冲区 end() 之前调用：把 [slotBegin, 当前槽位) 复制到回读缓冲。
		void CopyRecordedRange(vk::CommandBuffer cmdBuf, bool computeCommandBuffer, uint32_t slotBegin);
		uint32_t SlotCount(bool computeCommandBuffer) const;

		// 等待 GPU 完成、失效非一致映射内存、读回并换算成纳秒；失败返回 false。
		bool Finish(RenderBackend_Vulkan* pBackend);

		// ---- IGPUGraphTiming ----
		bool IsReady() const override { return m_Ready; }
		void Wait() override {}
		uint64_t SubmissionID() const override { return m_SubmissionID; }
		std::span<GPUPassTiming const> Passes() const override
		{
			return std::span<GPUPassTiming const>(m_Passes.data(), m_Passes.size());
		}

	private:
		struct PoolState
		{
			vk::QueryPool queryPool;
			vk::Buffer readbackBuffer;
			VmaAllocation readbackAllocation = VK_NULL_HANDLE;
			void* pMapped = nullptr;
			uint32_t slotCount = 0;   // 2 × 本池的 pass 数
			uint32_t nextSlot = 0;
			uint32_t openSlot = UINT32_MAX; // 当前打开 pass 的起点槽位；UINT32_MAX = 没有
			bool resetRecorded = false;     // vkCmdResetQueryPool 只在本池首次使用前记录一次
			uint32_t timestampValidBits = 64;
			castl::vector<uint64_t> ticks;
		};

		struct PendingPass
		{
			EGPUTimedPassType passType = EGPUTimedPassType::eRender;
			uint32_t passIndex = 0;
			EGPUQueueType queue = EGPUQueueType::eNone;
			castl::string name;
			uint32_t beginSlot = 0;
			bool computeCommandBuffer = false;
		};

		PoolState* GetPool(bool computeCommandBuffer);
		PoolState const* GetPool(bool computeCommandBuffer) const;
		bool InitPool(RenderBackend_Vulkan* pBackend, PoolState& pool, uint32_t passCount, int queueFamily);
		void ReleasePools();

		PoolState m_DirectPool;
		PoolState m_ComputePool;
		castl::vector<PendingPass> m_PendingPasses;
		castl::vector<GPUPassTiming> m_Passes;
		uint64_t m_SubmissionID = 0;
		float m_TimestampPeriod = 0.0f; // 纳秒 / tick（vk::PhysicalDeviceProperties::timestampPeriod）
		bool m_Ready = false;
	};
}
