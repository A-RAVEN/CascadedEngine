# gpu-graph-timing Specification

## Purpose
一次图执行返回逐 pass 的 GPU 起止时间：结果按图里的原始位置定位、生命周期独立于执行器、D3D12 与 Vulkan 经同一公共接口返回同形结果、不支持计时的队列显式失败，普通 `ExecuteGraph` 不承担计时开销。
## Requirements
### Requirement: 一次图执行必须返回逐 pass 的 GPU 时间

`CRenderBackend` SHALL 提供 `GetTimingCapabilities(queue)` 与 `ExecuteGraphTimed(scheduler, graph)`。`ExecuteGraphTimed` MUST 返回本次图执行的结果对象；在 `Wait()` 返回之后，`Passes()` MUST 给出每条实际执行的渲染 pass 与计算 pass 的起止 GPU 时间、实际队列和名称，且每条结果的 `endNs` MUST 不小于 `beginNs`。

#### Scenario: 含渲染与异步计算的图返回两条结果

- **WHEN** 一张图包含一个渲染 pass 和一个显式 `SetAsync(true)` 的计算 pass，调用 `ExecuteGraphTimed` 并 `Wait`
- **THEN** `Passes()` 恰好返回两条结果，`passType` 分别为 `eRender` 和 `eCompute`，`passIndex` 与图里的原始序号一致，`queue` 为该后端实际记录的队列，并且两条的 `endNs` 都大于等于 `beginNs`

#### Scenario: 计算 pass 走 direct 队列时也返回真实队列

- **WHEN** 计算 pass 未请求异步执行，或 Vulkan 后端没有独立计算队列，导致计算 pass 实际记录到 direct 队列
- **THEN** 该计算 pass 的结果仍为 `eCompute`，但 `queue` 必须为 `eDirect`，不得仅按 pass 类型伪报为 `eCompute` 队列

### Requirement: pass 结果必须按原始位置定位，不受批次重排影响

每条 `GPUPassTiming` MUST 用「pass 类型 + 该类型内的原始序号」标识自己在图里的位置；执行器的批次划分与依赖合并 MUST NOT 改变这个标识。名称只用于显示，不得作为定位依据。

#### Scenario: 依赖合并不改变结果里的 pass 序号

- **WHEN** 同一张图里有两个渲染 pass，第二个 pass 依赖第一个 pass 的输出
- **THEN** 两条结果的 `passIndex` 分别是图里的原始序号，不按批次执行顺序重新编号

### Requirement: 结果生命周期独立于执行器和帧上下文

计时结果 MUST 拥有自己的回读数据与同步资源。结果一经 `Wait()` 交付，后续图执行、帧上下文复用或执行器析构 MUST NOT 改变它的任何字段。

#### Scenario: 第二次执行后第一次结果逐字段不变

- **WHEN** 第一次 `ExecuteGraphTimed` 的结果在 `Wait()` 后被逐字段保存，随后执行一张 pass 结构不同的计时图
- **THEN** 第一次结果的 `SubmissionID`、pass 类型与序号、名称、队列、`beginNs` 和 `endNs` 都与保存的副本一致，第二次执行不得覆盖或改写第一次结果

### Requirement: 双后端必须通过同一公共接口返回同形结果

D3D12 与 Vulkan 的实现 MUST 都通过 `IGPUGraphTiming` 暴露结果，调用方 MUST NOT 需要向下转型到具体后端。两边对同一张图返回的 pass 类型与原始序号必须一致；`queue` 各自反映该后端的实际队列选择，允许因独立计算队列可用性不同而不同。

#### Scenario: 同一张图在两个后端返回相同的 pass 键

- **WHEN** 同一张包含一个渲染 pass 和一个计算 pass 的图分别在 D3D12 与 Vulkan 上执行 `ExecuteGraphTimed` 并 `Wait`
- **THEN** 两边结果的条数相同，`passType` 与 `passIndex` 集合相同，名称都非空，没有任何结果是未初始化数据，并且每边的 `queue` 都与该后端实际记录的队列一致

### Requirement: 不支持计时的队列必须显式失败

当图实际使用的队列不支持 pass 时间戳时，`ExecuteGraphTimed` MUST 返回空结果并记录原因，MUST NOT 返回空的成功结果、全零时间或部分伪造数据。

#### Scenario: 不支持的队列返回明确失败

- **WHEN** 图实际使用的队列满足 `GetTimingCapabilities(queue).passTimestamps == false`，仍调用 `ExecuteGraphTimed`
- **THEN** 返回空指针，并留下指出该队列不支持计时的错误信息；不得返回一个可 `Wait()` 且 `Passes()` 为空或全零的结果对象

### Requirement: 普通 ExecuteGraph 不得承担计时开销

普通 `ExecuteGraph` MUST NOT 创建时间戳查询堆/池、记录时间戳或等待计时回读；只有 `ExecuteGraphTimed` 才能启用这些行为。

#### Scenario: 普通入口不进入计时资源创建路径

- **WHEN** 用普通 `ExecuteGraph` 执行图，并运行 `python Tools/check_gpu_timing.py`
- **THEN** 两个后端的普通 `ExecuteGraph` 函数体都不包含计时入口或查询资源创建调用，检查脚本退出码为 0；若普通入口接入计时创建路径，脚本必须非零退出
