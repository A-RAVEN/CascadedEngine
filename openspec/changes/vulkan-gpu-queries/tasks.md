## 1. 公共接口与测试骨架

- [ ] 1.1 在 `Test/GPUBackendTester/Main.cpp` 增加 `TestGPUTiming` 与它的 `--test` 注册：构造一张含一个渲染 pass、一个显式 `SetAsync(true)` 计算 pass 的图，调用 `ExecuteGraphTimed`、`Wait()`、`Passes()`，断言两条结果、`passType`/`passIndex` 对得上、`queue` 是实际队列、`endNs >= beginNs`、名称非空。｜**验收**：测试函数与注册存在；此时接口尚未加，`python build.py --config Debug` 必须报找不到 `GPUTiming.h` 或未声明 `ExecuteGraphTimed`，把这段红态输出存进本 change 的 `evidence/timing-red.log`。本条只要求红态与测试骨架，不要求测试通过。
- [ ] 1.2 新增 `Interface/RenderInterface/header/GPUTiming.h`（`GPUTimingCapabilities` / `EGPUTimedPassType` / `GPUPassTiming` / `IGPUGraphTiming`），在 `CRenderBackend` 增加 `GetTimingCapabilities` 与 `ExecuteGraphTimed`，两个后端先补能力为假、返回空的 override。｜**验收**：`python build.py --config Debug` 通过；两个后端分别运行 `GPUBackendTester.exe --backend <后端> --test TestGPUTiming --headless 10` 都退出 1，stderr 都含 `当前后端不支持计时`，且不崩溃、不返回伪造的全零结果。

## 2. D3D12 计时实现

- [ ] 2.1 在 D3D12 后端新增查询资源与结果对象：按图实际使用的 direct / compute 队列分别创建时间戳查询堆和 CPU 回读缓冲，未使用的队列不创建；结果对象持有这些资源。在 `D3D12GPUGraphExecutor` 的 `BatchExecutionContext::Record` 里围绕 `BeginRenderPass` / `EndRenderPass` 与计算 `Dispatch` 记录起止，在 `Close()` 前解析。名称规则：渲染 pass 取 `RenderPass::GetName()`，无效时用 `render[<原始序号>]`；计算 pass 用 `compute[<原始序号>]`。｜**验收**：`GPUBackendTester.exe --backend d3d12 --test TestGPUTiming --headless 10` exit 0，`Passes()` 恰好两条；渲染 pass 为 direct，显式异步计算 pass 为 compute；两条均满足 `endNs >= beginNs`，名称符合上述规则。
- [ ] 2.2 让 D3D12 已交付结果在后续执行后保持不变：第一次图在 `Wait()` 后保存 `SubmissionID` 与全部 pass 字段，第二次执行一张 pass 结构不同的计时图，再逐项比较第一次结果。｜**验收**：该用例在 d3d12 exit 0；第一次结果的编号、pass 键、名称、队列和起止时间与保存副本完全一致。把结果改成指向帧上下文复用缓冲时，该用例必须失败，红态输出存入本 change 的 `evidence/` 后恢复。

## 3. Vulkan 计时实现

- [ ] 3.1 在 Vulkan 后端新增查询资源与结果对象：按图实际使用的队列分别创建 `vk::QueryPool` 和宿主可见回读缓冲，未使用的队列不创建；结果对象持有这些资源。写时间戳前在 render pass 外用 `vkCmdResetQueryPool` 初始化本次查询；在 `RecordRenderPass` / `RecordComputePass` 内记录起止；在 `endRenderPass()` 后、命令缓冲区 `end()` 前用 `VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT` 复制结果。回读缓冲带 `VK_BUFFER_USAGE_TRANSFER_DST_BIT`，64 位结果的偏移与步长按 8 字节对齐；`Wait()` 等对应队列完成后再读，非一致映射内存按既有内存管理方式失效缓存。名称规则与 2.1 相同。｜**验收**：`GPUBackendTester.exe --backend vulkan --test TestGPUTiming --headless 10` exit 0，两条结果、实际队列、名称与 `endNs >= beginNs` 判据都满足；`test_output/TestGPUTiming_validation.log` 不新增 VUID。
- [ ] 3.2 让 Vulkan 已交付结果在后续执行后保持不变：采用与 2.2 相同的“先保存副本、再执行不同结构图、逐字段比较”用例。｜**验收**：该用例在 vulkan exit 0，第一次结果所有字段与副本一致；把结果改成指向执行器或帧上下文复用资源时，该用例必须失败，红态输出存入本 change 的 `evidence/` 后恢复。

## 4. 双后端一致性与文档

- [ ] 4.1 同一张含渲染与显式异步计算 pass 的图分别在 d3d12 与 vulkan 上执行计时：两边结果条数相同，`passType` + `passIndex` 集合相同，名称都非空，每边队列都与该后端实际记录路径一致，不要求跨后端队列映射相同。再把计算 pass 改为 `SetAsync(false)`，两边该 pass 的 `queue` 都必须是 direct。｜**验收**：两个后端的 `--test TestGPUTiming --headless 10` 都 exit 0；删掉任一后端的 compute pass 查询，或把 direct 回退结果伪报为 compute 队列，该用例必须失败，红态记录后恢复。
- [ ] 4.2 `docs/testing.md` 增加 `TestGPUTiming`，测试总数从 10 改到 11，最小验证集合加入 vulkan 与 d3d12 两条计时命令；`GPUBackendTester.exe --list` 必须包含 `TestGPUTiming`。｜**验收**：`--list` 输出 11 条且含该名字；`docs/testing.md` 的测试清单与 `--list` 逐条一致，缺一条或名称不一致即失败。
- [ ] 4.3 删除 `docs/TODO.md` 的 [2026-07-12] `vulkan-gpu-queries` 条目，或把它改写成指向本 change 的一行指针；该条目现在写的“D3D12 已有查询”是错误事实。｜**验收**：`Select-String -Path docs/TODO.md -Pattern 'D3D12 通过'` 零命中；`D3D12_QUERY_HEAP` 在该文件零命中；本 change 归档后路线图才更新（不在本条）。

## 5. 收尾验证

- [ ] 5.1 新增 `Tools/check_gpu_timing.py` 并在 `docs/testing.md` 第三层登记：检查两个后端普通 `ExecuteGraph` 函数体不得调用计时入口或查询资源创建函数。随后跑 `python build.py --config Debug`、`CoreTests.exe`、两个后端的 `--test TestGPUTiming --headless 10`、以及普通路径的 `TestSimpleTriangle` 与 `TestIMGUI`。｜**验收**：全部 exit 0；`python Tools/check_gpu_timing.py` 为 0，临时在普通 `ExecuteGraph` 接入一次计时创建调用时必须非零，恢复后为 0；vulkan 的 `TestGPUTiming_validation.log` 无新增错误，d3d12 如生成同名日志也不得含错误。
- [ ] 5.2 跑 `openspec validate --all` 与 `openspec instructions apply --change vulkan-gpu-queries --json`。｜**验收**：validate 全过；apply 的 `remaining` 为 0；`git grep -n -F 'D3D12 通过' -- openspec/changes/vulkan-gpu-queries/proposal.md openspec/changes/vulkan-gpu-queries/design.md openspec/changes/vulkan-gpu-queries/specs` 零命中（不要把 tasks.md 放进路径，它自己含有这个检查串）。
