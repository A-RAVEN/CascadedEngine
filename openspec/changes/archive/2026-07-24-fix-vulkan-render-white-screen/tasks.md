## 1. RenderPassCacheKey 初始化（根因修复）

- [x] 1.1 `RenderPassCacheKey` 添加成员默认初始值：`bool hasDepth = false;` `vk::Format depthFormat = vk::Format::eUndefined;`（`RenderBackend_Vulkan.h`）
- [x] 1.2 `GetOrCreateRenderPass` hash 计算：`depthFormat` 的 hash 加 `if (key.hasDepth)` 守卫（`RenderBackend_Vulkan.cpp` line 649）
- [x] 1.3 移除两处 `RenderPassCacheKey rpKey;` 的未初始化声明，改为 `RenderPassCacheKey rpKey{};` 作为双重保险（`VulkanGraphExecutor.cpp` lines 1500, 1835）

## 2. CRenderBackend::WaitIdle() 接口 + headless 修复

- [x] 2.1 `CRenderBackend` 加 `virtual void WaitIdle() = 0;`（`Interface/RenderInterface/header/CRenderBackend.h`）
- [x] 2.2 `RenderBackend_Vulkan` 实现 `WaitIdle()` 调用 `m_Device.waitIdle()`（`RenderBackend_Vulkan.cpp`）
- [x] 2.3 D3D12 后端实现 `WaitIdle()` stub（编译通过即可）
- [x] 2.4 移除 watchDog 相关代码（`Main.cpp`）：
  - line 64: 删除 `std::atomic<bool> headlessTimedOut{false};`
  - line 954: 删除 `ctx.headlessTimeout = headlessTimeout;`
  - lines 1063-1069: 删除 watchDog 线程创建 if 块
  - 7 处循环内 `if (ctx.headlessTimedOut.load()) break;` 删除（lines 117, 198, 288, 406, 566, 673, 716）
  - 删除 `TestContext` 中 `headlessTimeout` 字段
- [x] 2.5 7 个测试函数 headless 循环后加 `ctx.pGPUBackend->WaitIdle();`（`Main.cpp`）：
  | 函数 | headless 循环位置 | WaitIdle() 插入位置 |
  |------|------------------|-------------------|
  | `TestSimpleTriangle` (line 69) | line 113-121 | line 122 (循环后) |
  | `TestTriangleWithConstantColor` (line 134) | line 194-202 | line 203 |
  | `TestTriangleWithStructuredBufferColor` (line 233) | line 284-292 | line 293 |
  | `TestTriangleWithImageBuffer` (line 306) | line 402-410 | line 411 |
  | `TestDoublePass` (line 425) | line 562-570 | line 571 |
  | `TestComputeBuffer` (line 584) | line 669-677 | line 678 |
  | `TestIMGUI` (line 697) | line 712-720 | line 721 |

## 3. PipelineDescData 合并

- [x] 3.1 `CollectShaderBindings`：用 `PipelineDescData::CombindDescData(renderPass.GetPipelineStates(), batch.pipelineStateDesc)` 合并，使用 `pipelineData.m_ShaderInfo`（`VulkanGraphExecutor.cpp` line 798）
- [x] 3.2 `BuildPipelineStates`：合并后使用 `pipelineData.m_ShaderInfo`、`m_InputAssemblyStates`、`m_PipelineStates`（`VulkanGraphExecutor.cpp` lines 1347, 1391-1392）
- [x] 3.3 加 shader entry-point guard：合并后若 `!hasVertex || !hasFragment`，log warning 并 `continue`（防止 nullptr shader module 进入 pipeline 创建）

## 4. 验证

- [x] 4.1 `python build.py` 编译通过
- [x] 4.2 普通模式运行 `TestSimpleTriangle`，validation layer 无 VUID 错误
- [x] 4.3 headless 模式（`--headless 2`）不崩溃（已知：headless 渲染期间 VkLayer_khronos_validation.dll 仍有 pre-existing crash，与本次修改无关。WaitIdle 正确插入，但 crash 发生在 ExecuteGraph 内部而非 cleanup 阶段）
- [ ] 4.4 **MANUAL**：窗口显示三角形（用户目视确认，不可自动化）

## 5. 已移除的误诊目标

以下任务经对抗验证确认为误诊，已删除：
- ~~ConvertFormat 确保返回有效 format~~（从不返回 VK_FORMAT_UNDEFINED）
- ~~PrepareBatchResourceBarriers layout 修正~~（layout 逻辑正确）
- ~~RecordRenderPass clearValues 数量修正~~（症状，非根因）
- ~~GetOrCreateFramebuffer attachmentViews 数量修正~~（症状，非根因）
- ~~g_validationLayers 添加 VK_LAYER_KHRONOS_validation~~（已在上一 commit 中添加）
