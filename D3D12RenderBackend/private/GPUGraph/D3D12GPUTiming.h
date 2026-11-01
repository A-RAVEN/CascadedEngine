#pragma once
// vulkan-gpu-queries（能力 `gpu-graph-timing`）任务 2.1：D3D12 的逐 pass GPU 计时结果对象。
//
// 设计要点（design D4/D5/D7）：
//  - 按图**实际使用的队列**分别创建时间戳查询堆（D3D12_QUERY_HEAP_TYPE_TIMESTAMP）与 CPU 回读缓冲
//    （READBACK 堆），未使用的队列不创建任何资源。
//  - 结果对象自己持有查询堆、回读缓冲、fence 与 CPU 数组；不引用执行器（执行器返回即析构），也不指向
//    帧上下文里会被 Reset() 复用的缓冲。
//  - 公开的只有纳秒（GPUPassTiming::beginNs / endNs），不暴露 ID3D12QueryHeap、tick 频率或有效位；
//    换算后 endNs < beginNs 或读取失败时整次计时显式失败（Finish() 返回 false）。
//
// 记录位置（任务 2.1）：`D3D12GPUGraphExecutor::BatchExecutionContext::Record` 里围绕
// `BeginRenderPass` / `EndRenderPass` 与计算 `Dispatch` 写起止时间戳，并在同一命令列表 `Close()` 之前
// 调 `ResolveRecordedRange` 解析。实现放在已有的 `GPUGraphExecutor.cpp`（本 change 不新增 .cpp，
// 理由见 `CRenderBackend.h` 里那段注释：新增源文件需要重新 configure，而本机当前无法联网配置）。

#include <CRenderBackend.h>
#include <GPUGraph.h>
#include <D3D12Includes.h>
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>

namespace graphics_backend
{
	class RenderBackend_D3D12;

	class D3D12GPUTiming : public IGPUGraphTiming
	{
	public:
		// 创建本次计时执行的结果对象：按图实际使用的 direct / compute 队列分配查询堆与回读缓冲。
		// 返回 nullptr 表示队列不支持计时或资源分配失败（原因已用 CA_LOG_ERR 记录，design D8）。
		static std::unique_ptr<D3D12GPUTiming> Create(
			RenderBackend_D3D12* pBackend,
			GPUGraph const& graph,
			uint64_t submissionID);

		// ---- 记录（由 D3D12GPUGraphExecutor 在命令列表上调用，顺序 = 提交顺序）----
		// 在 BeginRenderPass 之前 / dispatch 之前记录起点；EndRenderPass 之后 / dispatch 之后记录终点。
		void RecordPassBegin(ID3D12GraphicsCommandList7* pCmd, EGPUQueueType queue,
			EGPUTimedPassType passType, uint32_t passIndex, castl::string const& name);
		void RecordPassEnd(ID3D12GraphicsCommandList7* pCmd, EGPUQueueType queue);
		// 命令列表 Close() 之前调用：把 [slotBegin, 当前槽位) 解析到本队列的回读缓冲。
		void ResolveRecordedRange(ID3D12GraphicsCommandList7* pCmd, EGPUQueueType queue, uint32_t slotBegin);
		uint32_t SlotCount(EGPUQueueType queue) const;

		// ---- 执行之后 ----
		// 在队列上给本对象自己的 fence 发信号（信号排在已提交的图执行之后）。
		void SignalAfterExecution(RenderBackend_D3D12* pBackend);
		// 等待对应队列完成，读回原始 tick，换算成纳秒并校验；失败返回 false。
		bool Finish(RenderBackend_D3D12* pBackend);

		// ---- IGPUGraphTiming ----
		bool IsReady() const override { return m_Ready; }
		// 结果在 ExecuteGraphTimed 返回前已经同步完成（见 Finish），Wait() 只表示"数据已交付"。
		void Wait() override {}
		uint64_t SubmissionID() const override { return m_SubmissionID; }
		std::span<GPUPassTiming const> Passes() const override
		{
			return std::span<GPUPassTiming const>(m_Passes.data(), m_Passes.size());
		}

	private:
		struct QueueState
		{
			ComPtr<ID3D12QueryHeap> queryHeap;
			ComPtr<ID3D12Resource> readbackBuffer;
			ComPtr<ID3D12Fence> fence;
			uint64_t timestampFrequency = 0; // 本队列的 tick 频率（每毫秒 tick 数，D3D12 语义）
			uint32_t slotCount = 0;          // 2 × 本队列的 pass 数
			castl::vector<uint64_t> ticks;   // Finish 之后按槽位存放原始 tick
		};

		struct PendingPass
		{
			EGPUTimedPassType passType = EGPUTimedPassType::eRender;
			uint32_t passIndex = 0;
			EGPUQueueType queue = EGPUQueueType::eNone;
			castl::string name;
			uint32_t beginSlot = 0;
		};

		QueueState* GetQueueState(EGPUQueueType queue);
		QueueState const* GetQueueState(EGPUQueueType queue) const;
		bool InitQueue(RenderBackend_D3D12* pBackend, EGPUQueueType queue, uint32_t passCount);

		QueueState m_Direct;
		QueueState m_Compute;
		castl::vector<PendingPass> m_PendingPasses;
		castl::vector<GPUPassTiming> m_Passes;
		uint64_t m_SubmissionID = 0;
		// 每条队列自己的槽位游标：槽位按记录顺序（= 提交顺序）递增，begin/end 成对占用 2 个槽位。
		uint32_t m_NextSlotDirect = 0;
		uint32_t m_NextSlotCompute = 0;
		// 当前打开的 pass 的起点槽位；UINT32_MAX = 没有打开的 pass（end 找不到 begin 时报错，不写脏数据）。
		uint32_t m_OpenSlotDirect = UINT32_MAX;
		uint32_t m_OpenSlotCompute = UINT32_MAX;
		bool m_Ready = false;

		// fence 值：每条队列各一个 fence，都用 1；两个队列各等各的，不共用同一个 fencing 值
		// （同一个 fence 上"较大的值"不能证明另一条队列也执行完）。
		static constexpr uint64_t kFenceValue = 1;
	};
}
