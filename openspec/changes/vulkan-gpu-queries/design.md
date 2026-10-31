## Context

- `CRenderBackend::ExecuteGraph` 现在返回 `void`，`Interface/` 没有查询相关抽象。
- `GPUGraph` 暴露的是按类型分开的 pass 列表（`GetRenderPasses()` / `GetComputePasses()`）和阶段序列（`GetGraphStages()` / `GetPassIndices()`）；执行器会按依赖合并成批次，pass 的实际执行顺序不等于图里声明的顺序。
- 图执行器负责选择命令列表和队列。D3D12 的 `D3D12GPUGraphExecutor` 是 `RenderBackend_D3D12::ExecuteGraph` 内的栈上对象，Vulkan 的 `VulkanGraphExecutor` 也是同样用法，函数返回即析构。
- 两个后端的帧上下文都会被复用：D3D12 的 `GPUFrameManager` 和 Vulkan 的 `VulkanGPUFrameManager` 都按轮转返回上下文，`Reset()` 可以重置其中的命令列表、内存管理器与围栏。
- `catimer::TimerSystem` 目前只有 CPU `high_resolution_clock` 的 `BeginEvent` / `EndEvent` 与 `QueryHistories`，没有任何 GPU 数据。
- 两个后端都没有原生查询对象：D3D12 没有 `ID3D12QueryHeap` 的使用点，Vulkan 没有 `vk::QueryPool` 的创建点。

## Goals / Non-Goals

**Goals**

- 一次 `ExecuteGraphTimed` 返回本次实际执行的渲染 pass 与计算 pass 的 GPU 起止时间。
- 结果按「pass 类型 + 该类型内原始序号」定位，不受批次重排影响。
- 结果对象的生命周期独立于执行器和会被复用的帧上下文。
- D3D12 与 Vulkan 通过同一套公共接口返回结果，测试不需要向下转型。
- 普通 `ExecuteGraph` 路径的行为不受影响。

**Non-Goals**

- 遮挡查询与管线统计。
- 逐绘制调用计时。
- profiling 面板、GPU 历史存储和 CPU/GPU 时间轴对齐。
- 传输 pass 与 finalize pass 的计时（本次只定义渲染与计算两种 pass 类型）。
- 跨队列绝对时间比较与时钟校准。

## Decisions

### D1 公开的是图执行结果，不是查询命令

不提供 `CreateQueryPool` / `BeginQuery` / `EndQuery` 这类公共方法。调用方看不到实际命令列表、队列和批次划分，无法正确安放查询区间；只有图执行器知道每个 pass 最终记录在哪个命令缓冲区、哪条队列上。因此公共入口只描述“这一次图执行产生哪些 pass 时间”。

### D2 公共类型与入口

```cpp
struct GPUTimingCapabilities
{
    bool passTimestamps = false;
};

enum class EGPUTimedPassType
{
    eRender,
    eCompute,
};

struct GPUPassTiming
{
    EGPUTimedPassType passType;
    uint32_t passIndex;       // 该类型 pass 在图里的原始序号
    EGPUQueueType queue;      // 实际记录的队列（direct / compute）
    castl::string name;       // 结果对象自己持有；无名字时用类型与序号兜底
    uint64_t beginNs = 0;
    uint64_t endNs = 0;
    // 后端只把 endNs >= beginNs 的有效结果放进 Passes()；
    // 时间无效时整次计时失败，不提供把坏数据改成 0 的 DurationNs()。
};

class IGPUGraphTiming
{
public:
    virtual ~IGPUGraphTiming() = default;
    virtual bool IsReady() const = 0;
    virtual void Wait() = 0;
    virtual uint64_t SubmissionID() const = 0;
    virtual std::span<GPUPassTiming const> Passes() const = 0; // 只在 Wait 返回后读
};
```

`CRenderBackend` 新增两个入口，原 `ExecuteGraph` 保持原样：

```cpp
virtual GPUTimingCapabilities GetTimingCapabilities(EGPUQueueType queue) const = 0;
virtual std::unique_ptr<IGPUGraphTiming> ExecuteGraphTimed(
    TaskScheduler* scheduler,
    castl::shared_ptr<GPUGraph> const& graph) = 0;
```

`SubmissionID` 是后端为一次计时执行分配的单调编号，不是引擎帧号。现有测试在一次帧循环里会多次执行图，不能把一次图提交当成一帧。

### D3 pass 定位用「类型 + 原始序号」，名字只是显示信息

`GPUGraph` 已经保存原始 pass 列表，执行器里的 `rasterPassID` 与 `computePassID` 就是原始序号。结果不得按批次顺序编号，否则依赖合并后序号会变。渲染 pass 取 `RenderPass::GetName()`，名字无效时用 `render[<index>]`；计算 pass 没有名字，用 `compute[<index>]`。名字为空或无效时不能返回空字符串让调用方自己猜。

### D4 查询资源按队列分开，不能跨队列共用一对起止

渲染 pass 可能落在 direct 队列，计算 pass 可能落在 compute 队列（`ComputeBatch::asyncCompute` 为真且 compute 队列可用时）。`ComputeBatch::New()` 默认 `asyncCompute = false`，所以测试必须显式开启异步计算才能覆盖 compute 队列；Vulkan 没有独立计算队列时会合法地回退到 direct，双后端不得被要求永远返回相同的队列映射。

一对 begin/end 时间戳必须由同一个队列执行；跨队列的查询值不能直接相减，也不能把两条队列的时间简单相加。结果里的 `queue` 字段必须反映实际队列。查询资源按图实际使用的队列分配，未使用的队列不创建资源，避免让无关队列的能力或分配失败影响本次执行。

### D5 结果对象拥有自己的资源和数据

结果对象不能引用执行器（`ExecuteGraph` 返回即析构），也不能直接指向帧上下文里会被 `Reset()` 复用的查询池或回读缓冲。每次计时执行创建自己的查询资源与 CPU 回读数据，结果对象在 `Wait()` 完成前持有它们，并在析构时保证 GPU 已经不再引用。具体是每次新建还是从安全池里取，属于实现选择。

公共可验证的契约是：**结果一经 `Wait()` 交付，后续图执行与帧上下文复用不得改变它**。测试先保存第一次结果的逐字段副本，再执行一张 pass 结构不同的图，第一次结果的 `SubmissionID`、pass 键、名称和起止时间必须与副本完全一致。这样比“第二次之后第一次才读”更能确定性识别覆盖。

### D6 只在计时入口创建查询资源

普通 `ExecuteGraph` 继续走原路径，不创建查询堆/池、不等待回读、不改变提交顺序。`ExecuteGraphTimed` 才创建查询资源并等待/提供结果。profiling 应该是显式打开的能力，不能让所有渲染帧默认承担查询开销。新增 `Tools/check_gpu_timing.py` 做结构检查：两个后端普通 `ExecuteGraph` 函数体不得调用计时入口或查询资源创建函数；违反时脚本非零退出。

### D7 把设备 tick 换成纳秒，公开结果不暴露原生时间戳

后端读取设备时间戳后，按设备的 tick 周期换算成纳秒，并处理有效位不足 64 位时的回绕/掩码。公开结果只给纳秒，不暴露 `vk::QueryPool`、`ID3D12QueryHeap`、tick 周期或有效位。这样调用方不需要知道后端差异，也不会把原始 tick 当纳秒用。换算后若出现 `endNs < beginNs`、结果不可用或查询失败，整次计时必须显式失败，不得把无效值截断成 0 或继续返回。

### D8 不支持时显式失败，不返回伪造的零值

`GetTimingCapabilities(queue).passTimestamps` 为假时，调用方不应发起计时。若图包含不支持计时的队列上的 pass，`ExecuteGraphTimed` 返回空结果并记录原因；不得返回一组 duration 为 0 的结果冒充成功。设备支持但与实现预期不符的情况同样按显式失败处理。

### D9 不引入固定上限和固定回读延迟

查询数量按本次实际执行的 pass 数动态分配，不写死任何槽数上限；结果是否就绪由 `IsReady()` / `Wait()` 表达，不承诺固定帧数之后才能读。旧工件里的固定槽数和固定回读延迟都没有代码或用户要求作为依据，删除。

### D10 Vulkan 查询必须先初始化，复制时必须可用

每个查询池在写时间戳之前，用 `vkCmdResetQueryPool` 初始化本次要用的查询区间；重置命令和时间戳结束后的结果复制都放在 render pass 外。结果复制使用 `VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT`：64 位标志保证公开换算拿到完整时间戳，`WAIT_BIT` 让时间戳写入纳入复制的执行依赖。若以后改成非等待复制，就必须改用可用性标志并逐条检查，不能把不可用值当结果。

回读缓冲必须带 `VK_BUFFER_USAGE_TRANSFER_DST_BIT`，64 位结果的偏移与步长按 8 字节对齐。`Wait()` 等待对应队列完成后才允许 CPU 读取；非一致映射内存按后端既有内存管理方式失效缓存。复制命令本身只能在 `endRenderPass()` 之后、命令缓冲区 `end()` 之前记录。

依据：[vkCmdResetQueryPool](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdResetQueryPool.html)、[vkCmdCopyQueryPoolResults](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyQueryPoolResults.html)。后者明确要求查询不能未初始化、结果必须已经可用，并说明未指定 `WAIT_BIT` 时 `vkCmdWriteTimestamp` 不在第一同步范围内。

## Risks / Trade-offs

- **设备时间戳能力不一致**：某些队列的 `timestampValidBits` 可能为 0，或有效位少于 64 位。实现必须按队列查询能力并处理掩码；不能假设所有 GPU 都一样。
- **队列时间域不同**：即使都换算成纳秒，不同队列的绝对时间也不一定能直接比较。本次只保证单条 pass 的时长与同队列内的先后，不承诺跨队列绝对时间。
- **D3D12 命令列表解析位置**：解析命令必须在查询记录之后的同一个命令列表里、`Close()` 之前记录；每条队列各用一套查询资源。
- **Vulkan 命令缓冲区与查询池生命周期**：查询池和回读缓冲要活到队列执行完成；不能跟着执行器析构。
- **headless 与无窗口场景**：计时不应依赖 present 或窗口存在；测试要在 headless 下也能跑。
- **每次计时新建资源的开销**：本次优先保证生命周期正确，profiling 只在显式调用时发生；以后有数据再考虑池化。
- **面板接入会引入时钟语义**：`TimerSystem` 的 `time_point` 是 CPU 时钟，GPU 纳秒不能直接塞进去；后续 change 必须另设数据通道。
