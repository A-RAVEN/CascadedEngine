#pragma once
#include "Common.h"
#include <CASTL/CAString.h>
#include <cstdint>
#include <memory>
#include <span>

namespace graphics_backend
{
	// vulkan-gpu-queries（能力 `gpu-graph-timing`）：一次图执行的逐 pass GPU 计时。
	//
	// 公共面只描述"这次图执行产生哪些 pass 时间"，不暴露 vk::QueryPool / ID3D12QueryHeap，
	// 也不提供 CreateQueryPool / BeginQuery / EndQuery 这类命令级接口（design D1）——
	// 调用方看不到实际命令列表、队列与批次划分，无法正确安放查询区间。

	// 每个队列的计时能力（design D2）。`passTimestamps` 为假时调用方不应发起计时；
	// 图实际使用的队列不支持时 `ExecuteGraphTimed` 返回空结果并记录原因（design D8）。
	struct GPUTimingCapabilities
	{
		bool passTimestamps = false;
	};

	// 本次定义的两种可计时 pass（design D2 / Non-Goals：传输 pass 与 finalize pass 不计时）。
	enum class EGPUTimedPassType
	{
		eRender,
		eCompute,
	};

	// 单条 pass 的 GPU 起止时间。
	// 定位用「pass 类型 + 该类型内的原始序号」，批次重排不改变标识；`name` 只用于显示（design D3）。
	// 后端只把 `endNs >= beginNs` 的有效结果放进 `Passes()`；时间无效时整次计时显式失败，
	// 不提供把坏数据改成 0 的 DurationNs()（design D7）。
	struct GPUPassTiming
	{
		EGPUTimedPassType passType = EGPUTimedPassType::eRender;
		uint32_t passIndex = 0;              // 该类型 pass 在图里的原始序号
		EGPUQueueType queue = EGPUQueueType::eNone; // 实际记录的队列（direct / compute）
		castl::string name;                  // 结果对象自己持有；无名字时用类型与序号兜底
		uint64_t beginNs = 0;
		uint64_t endNs = 0;
	};

	class IGPUGraphTiming
	{
	public:
		virtual ~IGPUGraphTiming() = default;
		virtual bool IsReady() const = 0;
		virtual void Wait() = 0;
		// 后端为一次计时执行分配的单调编号，不是引擎帧号（design D2）。
		virtual uint64_t SubmissionID() const = 0;
		// 只在 Wait() 返回之后读。结果对象自己持有回读数据，后续图执行不得改变它（design D5）。
		virtual std::span<GPUPassTiming const> Passes() const = 0;
	};
}
