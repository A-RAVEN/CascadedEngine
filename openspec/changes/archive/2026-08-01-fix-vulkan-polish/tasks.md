<!-- 审计标注: 2026-08-01 baseline audit — Section 4 大面积过时, Section 2 文件路径修正, 新增 5 个 [AUDIT] task, 详见 Review Log -->

## 1. Descriptor Pool 生命周期约束记录

- [x] 1.1 在 `VulkanFrameContext::Aquire()` 中 `resourceManager.ResetDescriptorPool()` 调用处添加注释，说明 `vkResetDescriptorPool` 隐式释放池内所有 descriptor set，新分配的 set 内容为空，每帧必须全量重写——这是 descriptor 写入缓存不可行的根本原因
- [x] 1.2 在 `VulkanResourceBindingInstance::BuildDescriptors()` 头部添加注释，说明当前每帧全量重建所有 binding 的 `vkWriteDescriptorSet` 是因为 FrameContext 每帧 reset pool 导致 descriptor set 内容为空白（空 set 不含任何有效描述），并标注 TODO: future work — 若引入跨帧 descriptor set 复用（需改动 pool 生命周期策略，见 design.md D1），可在此处实现写入缓存
- [x] 1.3 在 `VulkanResourceBindingInstance.h` 类定义处添加文档注释，说明 descriptor 写入缓存的设计约束与 future work 方向（三种可选方案：FREE_DESCRIPTOR_SET_BIT、跨帧复用、多 pool 轮转；注意 current pool 已使用 `eFreeDescriptorSet` flag 但 `resetDescriptorPool` 仍清空所有 set）
- [x] 1.4 [AUDIT] 在 `VulkanFrameBoundResourceManager::ResetDescriptorPool()` 方法定义处（`VulkanFrameManager.cpp:83-89`）添加注释，说明 `vkResetDescriptorPool` 的 Vulkan spec 行为：隐式释放池内所有 descriptor set、新 set 需全量写入

## 2. Debug 命名

- [x] 2.1 检查 `RenderBackend_Vulkan` 初始化流程中 `VK_EXT_debug_utils` 扩展的启用状态：确认 `GetInstanceExtensionNames()`（`RenderBackend_Vulkan.cpp:30`）始终包含 `VK_EXT_DEBUG_UTILS_EXTENSION_NAME`（无 `#ifndef NDEBUG` 守卫）；确认 `SetVKObjectDebugName` 的 `setDebugUtilsObjectNameEXT` 调用需要守卫
- [x] 2.2 在 `VulkanMemoryManager::CreateBuffer()` / `CreateImage()` 中调用 `SetVKObjectDebugName`（若扩展启用）。注意：`VulkanBuffer::SetName()` 和 `VulkanTexture::SetName()` 已调用 `SetVKObjectDebugName`，此 task 针对绕过 `VulkanBuffer`/`VulkanTexture` 的直接 VMA 分配路径。审计确认主要路径已被 SetName() 覆盖；`VulkanLinearMemoryManager` staging buffer 路径在 `VulkanMemoryManager` 内无独立命名点（被 `VulkanBuffer::SetName()` 覆盖），已满足需求。
- [x] 2.3 在 `VulkanGraphLocalResourceManager` 的 buffer/image 创建点调用 `SetVKObjectDebugName`（若扩展启用）。审计确认：临时资源在 aliased pool 中绑定，无独立的 `vk::Buffer`/`vk::Image` 创建调用可插入命名。debug 命名已通过 `VulkanBuffer`/`VulkanTexture` 包装层覆盖主要路径。
- [x] 2.4 在 `VulkanPipelineLibrary` 各 `Create*Pipeline()` / `LinkPipeline()` 方法中调用 `SetVKObjectDebugName`，传入 shader 名称
- [x] 2.5 在 `VulkanCommandListManager` 的 `GraphicsCommand()` / `ComputeCommand()` / `TransferCommand()` 中，分配 command buffer 后调用 `SetVKObjectDebugName`
- [x] 2.6 在 `VulkanResourceBindingInstance::AllocateDescriptorSets()` 中，为每个分配的 descriptor set 调用 `SetVKObjectDebugName`
- [x] 2.7 在 `RenderBackend_Vulkan::GetOrCreateShaderModule()`（`RenderBackend_Vulkan.cpp:631`）中为 `vk::ShaderModule` 调用 `SetVKObjectDebugName`，名称格式 `"ShaderMod:<handle>"`
- [x] 2.8 在 `RenderBackend_Vulkan::GetOrCreateFramebuffer()`（`RenderBackend_Vulkan.cpp:815`）中为 `vk::Framebuffer` 调用 `SetVKObjectDebugName`，名称格式 `"Framebuf:<WxH>"`
- [x] 2.9 在 `RenderBackend_Vulkan::GetOrCreateRenderPass()`（`RenderBackend_Vulkan.cpp:780`）中为 `vk::RenderPass` 调用 `SetVKObjectDebugName`，名称格式 `"RenderPass:<colorCount>c<hasDepth?+d>"`
- [x] 2.10 [AUDIT] 在 `SetVKObjectDebugName` 函数体（`VulkanDebug.h:22`）中为 `device.setDebugUtilsObjectNameEXT(nameInfo);` 添加 `#ifndef NDEBUG` / `#endif` 守卫，与 `m_DebugMessenger` 创建（`RenderBackend_Vulkan.cpp:234-236`）的策略一致
- [x] 2.11 [AUDIT] 在 `RenderBackend_Vulkan::GetOrCreatePipelineLayout()` 中为 `vk::PipelineLayout` 调用 `SetVKObjectDebugName`，名称格式 `"PipelineLayout:<hash>"`
- [x] 2.12 [AUDIT] 在 `VulkanFrameBoundResourceManager::CreateFences()` 中为 `m_DirectFence`/`m_ComputeFence` 调用 `SetVKObjectDebugName`（`"Fence:direct"` / `"Fence:compute"`）；在 `AllocCrossQueueSemaphore()` 中为新建 semaphore 调用 `SetVKObjectDebugName`（`"Semaphore:cq<N>"`）

## 3. PipelineLibrary Hash 查找

- [x] 3.1 在 `VulkanPipelineLibrary.h` 中定义 `PipelineLibraryKeyHash` functor，从三个 `VKHashVal`（各 32-byte SHA-256）各取首 8-byte XOR 得 `size_t`；同时定义 `PipelineLibraryKeyEqual` functor（调用 `PipelineLibraryKey::operator==` 做完整三字段 compare），作为 `unordered_map` 的 `key_eq` 回退
- [x] 3.2 将 `m_LibraryCache` 类型从 `castl::vector<castl::pair<...>>` 改为 `castl::unordered_map<PipelineLibraryKey, PipelineLibraryParts, PipelineLibraryKeyHash, PipelineLibraryKeyEqual>`
- [x] 3.3 更新 `GetCachedLibrary()` 使用 `unordered_map::find()` 替代线性搜索
- [x] 3.4 更新 `CacheLibrary()` 使用 `unordered_map::insert()` 或 `operator[]` 替代 `push_back()`
- [x] 3.5 在 `PipelineLibraryKeyHash` 实现处添加注释，说明 64-bit hash 的碰撞风险（birthday bound ~2^32）及 `PipelineLibraryKeyEqual` 完整 key compare 回退机制的安全网作用

## 4. CommandList 防御性加固

- [x] ~~4.1 OBSOLETE — 架构已变：`m_GraphicsCommand`/`m_ComputeCommand` 成员不存在，当前使用 vector 追踪 + Reset 时 `freeCommandBuffers` 的正确模式~~
- [x] ~~4.2 OBSOLETE — 无需要移除置零的成员变量~~
- [x] ~~4.3 OBSOLETE — 无懒加载复用 pattern 需要添加状态标志~~
- [x] ~~4.4 OBSOLETE — 无状态标志需要 Init/Destroy 管理~~
- [x] 4.5 在 `VulkanCommandListManager::Reset()` 方法头部添加 `CA_ASSERT` 或注释，说明调用方必须已通过 `SubmitBatches` 的 per-batch `waitForFences` + `resetFences` 确保 GPU 完成（参考 `VulkanFrameContext::Aquire()` 第 187-194 行已有同步文档）
- [x] 4.6 Fence 同步排序已文档化 — `VulkanFrameContext::Aquire()` 第 187-194 行已明确说明 `SubmitBatches` 的 per-batch fence 等待保证 GPU 完成后再 Reset
- [x] 4.7 [AUDIT] 在 `VulkanCommandListManager::Release()` 中 `destroyCommandPool` 之前，对 `m_AllocatedGraphicsCmdBufs` / `m_AllocatedComputeCmdBufs` 调用 `freeCommandBuffers` 清理（防御性编程，虽 `destroyCommandPool` 会隐式释放，但显式清理更安全）

## 5. Post-Compile Review 修复 (Round 3 [AUDIT])

- [x] 5.1 [AUDIT] `VulkanPipelineLibrary.h` `PipelineLibraryKeyHash`: 将 `reinterpret_cast<size_t const*>` 替换为 `memcpy` 消除 strict aliasing UB（`VKHashVal` = `unsigned char[32]`, align 1 → `size_t` align 8）。`memcpy` 需 `<cstring>`
- [x] 5.2 [AUDIT] `RenderBackend_Vulkan.cpp` 添加 `#include <string>`（4 处 `std::string` + `std::to_string` 构造，无显式 include）
- [x] 5.3 [AUDIT] `VulkanFrameManager.cpp` 添加 `#include <string>`（line 72 `Semaphore:cq` 构造）
- [x] 5.4 [AUDIT] `VulkanResourceBindingInstance.cpp` 添加 `#include <string>`（line 545 `DescSet:set` 构造）
- [x] 5.5 [AUDIT] 6 处 `std::string` debug name 构造加 `#ifndef NDEBUG` 守卫，避免 release build 无用 heap 分配：`RenderBackend_Vulkan.cpp`(632/709/785/822)、`VulkanFrameManager.cpp`(72)、`VulkanResourceBindingInstance.cpp`(545)。注：`VulkanPipelineLibrary.cpp`/`VulkanCommandListManager.cpp` 的 debug name 是字符串字面量，无 heap 分配，无需守卫
- [x] 5.6 [AUDIT] 修复 12 行 `SetVKObjectDebugName` / `std::string` 缩进不一致（`VulkanPipelineLibrary.cpp` 6处 1tab→2tab、`RenderBackend_Vulkan.cpp` 4处 3tab→2tab、`VulkanResourceBindingInstance.cpp` 1处 5tab→4tab、`VulkanCommandListManager.cpp` 1处 1tab→2tab）
- [x] 5.7 [AUDIT] 修正 `PipelineLibraryKeyHash` 注释：明确"8 bytes"仅适用于 64-bit（`sizeof(size_t) == 8`），32-bit 下仅 4 bytes（birthday bound ~2^16）

## 6. 编译验证

- [x] 6.1 运行 `python build.py`，验证 BUILD SUCCESSFUL
- [x] 6.2 若有编译错误或警告，分析并修复后重新验证

## 7. Review & Adversarial Verify (Round 4)

- [x] 7.1 对 Section 5（Round 3 [AUDIT] 修复）做对抗验证审查（2-agent workflow），验证正确性/完整性
- [x] 7.2 审查发现的新问题作为 [AUDIT] task 追加，追加新的审查 task，循环直到无新问题或 10 轮

## 8. Round 4 审查发现修复

- [x] 8.1 [AUDIT] `VulkanPipelineLibrary.cpp` 6 处 `SetVKObjectDebugName` 缩进 2 tabs → 3 tabs（`try {` 块内代码为 3 tabs，5.6 声称修复但实际误判未修）
- [x] 8.2 [AUDIT] `RenderBackend_Vulkan.cpp:127-133` Task 4.3 块（`g_ValidationLogFile` 写入 + `CA_LOG_ERR`）4 tabs → 3 tabs 过度缩进（pre-existing，但本 change 涉及文件，顺手修复）

## 9. Review & Adversarial Verify (Round 5)

- [x] 9.1 对 Section 8（Round 4 审查发现修复）做对抗验证审查，验证正确性/完整性
- [x] 9.2 审查发现的新问题作为 [AUDIT] task 追加，循环直到无新问题或 10 轮

## Review Log

| Round | Date | Agents | Issues Found | Issues Fixed | Remaining |
|-------|------|--------|-------------|-------------|-----------|
| 0 (baseline audit) | 2026-08-01 | 3 (code-structure, logic, synthesis) | 10 task status changes + 5 new tasks | N/A (artifact update only) | 21 tasks pending implementation |
| 1 (implementation) | 2026-08-01 | manual | 32 tasks implemented (5 [AUDIT] + 4 OBSOLETE + 1 ALREADY_DONE) | 32 | 3 (build + review) |
| 2 (adversarial review) | 2026-08-01 | 3 (correctness, completeness, synthesis) | BUG-1 [HIGH] semaphore off-by-one; ISSUE-2/3; GAP-1/2/3; DC-1 | BUG-1 fixed; others LOW/out-of-scope | 0 (all BLOCKING resolved) |
| 3 (post-compile review) | 2026-08-01 | 3 (correctness, completeness, synthesis) | NEW-1 [PORT] missing `#include <string>` x3; NEW-2 [DOC] 32-bit comment; F1 [BUG] reinterpret_cast UB; F2 [PERF] release string allocs; F3 [STYLE] 12 indent errors | 0 (all new) | 7 new [AUDIT] tasks (Section 5) |
| 4 (Round 3 fixes review) | 2026-08-01 | 3 (correctness, completeness, synthesis) | 误报 x1 (callback NDEBUG 内 std::string); S1 [STYLE] PipelineLibrary 6 处缩进 2→3 tabs (5.6 误判未修); S2 [STYLE] RenderBackend 127-133 过度缩进 | 8.1, 8.2 fixed; 误报已验证为 NDEBUG 内代码 | 2 new [AUDIT] tasks (Section 8) |
| 5 (Section 8 fixes review) | 2026-08-01 | 2 (review, synthesis) | 无 in-scope 问题。2 个疑似缩进问题经 git diff HEAD 验证为 pre-existing（Init try 块 L266-346、第二 namespace 块 L604-853），不在本 change hunk 内 | N/A | 0 — 循环终止 |

### Round 4-5 Detail

**Round 4**: 审查 Round 3 修复。验证 memcpy lambda 正确（`VKHashVal` 为 standard-layout struct，`&h` == `&h.data[0]`）、6 处 NDEBUG 守卫位置正确、4 个 include 无冲突。发现 2 个真实问题：8.1 (PipelineLibrary 6 处缩进 2→3 tabs，5.6 声称修复但实际误判——我此前 grep 误将 2 tabs 判为正确) 和 8.2 (RenderBackend 127-133 过度缩进，pre-existing 但顺手修)。1 个误报（debugUtilsMessengerCallback 的 std::string 已整体在 `#if !defined(NDEBUG)` 内）。

**Round 5**: 审查 Section 8 修复。全部 PASS。全文件缩进扫描发现 2 个疑似问题（Init try 块 L266-346、第二 namespace 块 L604-853 各缺 1 tab），但合成代理独立验证：`git diff HEAD` 无相关 hunk、`git show ea9dd11` 中同样存在——均为 pre-existing，不在本 change 范围内。**本 change 所有改动与其周围代码缩进一致，无 bug 无 style 问题，循环终止。**

### Round 3 Detail

**编译验证**: `python build.py` → BUILD SUCCESSFUL（仅 VulkanRenderBackendNew 模块重新链接）。测试行为与 baseline 一致（crash 非本次改动引入，见 `fix-vulkan-test-crash` proposal）。

**新发现问题（之前 3 轮全部漏掉）**:
- NEW-1 [PORT]: `VulkanPipelineLibrary.cpp`、`VulkanFrameManager.cpp`、`VulkanResourceBindingInstance.cpp` 使用 `std::string`/`std::to_string` 但无 `#include <string>`。MSVC 碰巧通过间接 include 编译通过，Clang/GCC strict mode 会失败。
- NEW-2 [DOC]: `PipelineLibraryKeyHash` 注释 "8 bytes (size_t)" 仅适用于 64-bit。32-bit 下 `sizeof(size_t)==4`，hash 仅 32-bit（birthday bound ~2^16）。
- F1 [BUG]: `reinterpret_cast<size_t const*>` 从 `unsigned char[32]` (align 1) 读取是 strict aliasing UB。替换为 `memcpy`。
- F2 [PERF]: ~14 处 `std::string` 临时对象在 release build (`NDEBUG`) 中无用分配（`SetVKObjectDebugName` 编译为空函数）。
- F3 [STYLE]: 12 行 debug name 代码缩进不一致（awk 插入时未匹配周围代码缩进层级）。

**之前漏掉的原因**: Round 0-2 的审查均未基于编译通过的代码——审查代理读取源文件文本，侧重"task 描述是否实现"，未检查 include 依赖、release build 行为、缩进风格。

### Round 2 Detail

**BUG-1 [HIGH] — FIXED**: `AllocCrossQueueSemaphore` 在新建 semaphore 后将 index 设为 `size() - 1`，导致下一次调用复用同一个 semaphore（frame 内重复 handle）。修复：`size() - 1` → `size()`。同时将 debug name 从 1-based 改为 0-based (`size() - 1`)。

**Verified Correct (all 7 checklist items)**:
- `#ifndef NDEBUG` 守卫正确包裹整个函数体
- `PipelineLibraryKeyEqual::operator==` 正确使用 C++20 默认 `operator<=>` 生成的三字段比较
- `unordered_map::insert` 语义正确（不覆盖已有 key）
- `Release()` 中 transfer command buffer 的 nullptr 检查正确处理未初始化和已释放状态
- 所有 debug name 的 `std::string` 临时对象生命周期正确（表达式结束前已传给 `SetVKObjectDebugName`）
- `AllocateDescriptorSets` 的 set index 使用 `setLayoutPairs[i].first` 正确
- Descriptor pool 生命周期注释准确描述 Vulkan Spec §12.2.2 行为

**Pre-existing / Out-of-scope (未修复，LOW 优先级)**:
- ISSUE-3: `GetCachedLibrary` 返回 raw pointer into `unordered_map`（与原来 vector 实现相同 pattern，非本 change 引入）
- GAP-1: 编译验证因 VS SDK 环境问题未完成（`cstdarg`/`stddef.h` not found），非代码问题
- GAP-2: 5 个 Vulkan 对象（CommandPool, DescriptorPool, DescriptorSetLayout, PipelineCache, WindowSync semaphore）缺少 debug name，超出原 task scope
- GAP-3: DescSet 命名格式未包含 shader name，与 design.md 有偏差
- DC-1: `PipelineLibraryKeyHash` 的 `reinterpret_cast` 在严格意义上 UB（alignment 1 → 8），但在 x64 上实际安全，已有注释说明
| 1 (implementation) | 2026-08-01 | manual | 24/28 tasks implemented (4 OBSOLETE + 1 ALREADY_DONE + 19 new edits) | N/A | 3 (build + review) |
