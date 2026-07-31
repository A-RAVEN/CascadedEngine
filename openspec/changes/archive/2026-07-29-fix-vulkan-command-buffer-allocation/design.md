# Design: 修复 Vulkan 命令缓冲区分配失败

**Change ID**: fix-vulkan-command-buffer-allocation
**Created**: 2026-07-15

---

## Context

### 问题现象

```
TestSimpleTriangle --backend vulkan --headless 5
  → VulkanCommandListManager initialized successfully (×3)
  → Swapchain created ✓
  → Resource aliasing ✓
  → Internal buffer not registered (×2, 警告)
  → CRASH: AllocateCommandBuffer line 125
       allocateCommandBuffers() 返回空 vector
       → VK_RESULT_CHECK 断言触发
```

### 调用链

```
RenderBackend_Vulkan::ExecuteGraph
  → VulkanGraphExecutor::CompileAndExecute
    → Execute
      → RecordBatchCommands
        → resourceManager.GetCommandListManager()  // FrameContext 的实例
          → GraphicsCommand()
            → AllocateCommandBuffer(m_GraphicsPool)  // 返回空 → CRASH
```

### 关键代码

`VulkanCommandListManager::AllocateCommandBuffer` (line 117-127):

```cpp
vk::CommandBuffer VulkanCommandListManager::AllocateCommandBuffer(vk::CommandPool pool)
{
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = pool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    auto cmdBufs = GetDevice().allocateCommandBuffers(allocInfo);
    VK_RESULT_CHECK(cmdBufs.size() > 0 ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY);
    return cmdBufs[0];
}
```

**问题 1**: 当前代码丢弃了 `vkAllocateCommandBuffers` 的真实失败原因。本项目 vulkan.hpp 为 throwing 模式（未定义 `VULKAN_HPP_NO_EXCEPTIONS`），`allocateCommandBuffers` 返回 `std::vector<vk::CommandBuffer>`，失败时可能抛出 `vk::SystemError` 异常或返回空 vector。当前代码仅检查 `cmdBufs.size() > 0`，既没有捕获异常也没有在空 vector 时输出有用诊断信息。

**问题 2**: FrameContext 内嵌的 `VulkanCommandListManager` 的 `m_GraphicsPool` 分配失败。经过对抗审查确认：
- pool 创建时 `device.createCommandPool()` 返回直接值（非 `ResultValue`），失败会抛异常 → 若 Init() 完成，pool 一定有效
- 所有 subobject 共享同一个 `RenderBackend_Vulkan*`（通过 `SetApp` 链传播），`GetDevice()` 始终返回相同的 `vk::Device` → 不存在跨 Device 使用 pool
- 两条初始化路径（全局和 FrameContext）等价：均使用零参 `InitSubObj`（仅 `SetApp`）+ 显式 `Init()`
- 真正差异点未知，需要运行时诊断确定

### FrameContext CommandListManager 的初始化

有两个路径初始化 `VulkanCommandListManager`：

**路径 A — 全局实例** (`RenderBackend_Vulkan::Init`):
```cpp
InitSubObj(&m_CommandListManager, /* no args */);
m_CommandListManager.Init();
// InitSubObj（无参版本）仅调用 SetApp，不调用 Init
// 然后显式调用 m_CommandListManager.Init() 创建 pool
```

**路径 B — FrameContext 实例** (`VulkanFrameBoundResourceManager::Init`):
```cpp
auto pApp = GetApp();
pApp->InitSubObj(&m_CommandListManager);  // SetApp only
m_CommandListManager.Init();              // 显式 Init
```

两条路径等价，都不是双重 Init。

### FrameContext 的生命周期

```
GPUFrameManager::Init → 创建 2 个 FrameContext，每个调用 Init()
  第一帧: AquireFrameContext() → context[0].Aquire()
    → m_FirstFrame=true → CreateFences()
    → resourceManager.Reset()
        → m_CommandListManager.Reset()
          → resetCommandPool(m_GraphicsPool)  ← 对从未分配过的 pool 调用 reset
    → RecordBatchCommands → GraphicsCommand() → AllocateCommandBuffer → CRASH
  第二帧: (永远不会到达)
```

**⚠️ 首次帧 Reset 风险**（对抗审查发现）：在 `m_FirstFrame=true` 分支中，`resourceManager.Reset()` 对刚创建、从未分配过的 pool 调用 `vkResetCommandPool`。虽然 Vulkan 规范允许在未使用的 pool 上调用 reset，但某些 GPU 驱动实现可能在此场景下行为异常——恰好导致"pool 创建成功 → reset → 分配失败"的症状。首次帧所有状态（`m_GraphicsCommand`、fence、`m_CrossQueueSemaphoreIndex`）已是默认值，跳过 Reset 完全安全且零成本。

---

## Goals / Non-Goals

**Goals:**
- 修复 FrameContext 的 `VulkanCommandListManager` 命令缓冲区分配失败
- 添加 VkResult 诊断日志，使后续类似问题可快速定位
- 确保 `TestSimpleTriangle` (headless) 可成功运行

**Non-Goals:**
- 不修改全局 `VulkanCommandListManager`（该实例工作正常）
- 不修改命令缓冲区录制内容
- 不修改帧调度逻辑

---

## Decisions

### D1: 添加异常/错误诊断，再根据错误码定位根因

**选择**: 在 `AllocateCommandBuffer` 中用 try-catch 包裹 `allocateCommandBuffers` 调用，捕获 `vk::SystemError` 异常并输出 `e.what()`；同时在空 vector 时输出 pool 原始句柄等诊断信息。

**理由**: 本项目的 vulkan.hpp 为 throwing 模式，`allocateCommandBuffers` 返回 `std::vector<vk::CommandBuffer>`（不是 `ResultValue`），失败时抛异常。当前代码既没有 try-catch，也没有检查空 vector 时输出 pool 状态。

**实现**:
```cpp
vk::CommandBuffer VulkanCommandListManager::AllocateCommandBuffer(vk::CommandPool pool)
{
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = pool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    try
    {
        auto cmdBufs = GetDevice().allocateCommandBuffers(allocInfo);
        if (cmdBufs.empty())
        {
            CA_LOG_ERR("VulkanCommandListManager: allocateCommandBuffers returned empty vector, pool={}",
                static_cast<VkCommandPool>(pool));
        }
        VK_RESULT_CHECK(!cmdBufs.empty() ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY);
        return cmdBufs[0];
    }
    catch (vk::SystemError const& e)
    {
        CA_LOG_ERR("VulkanCommandListManager: allocateCommandBuffers threw: {}, pool={}",
            e.what(), static_cast<VkCommandPool>(pool));
        throw; // re-throw, let VK_RESULT_CHECK or caller handle
    }
}
```

**可能的结果与应对** (per Vulkan 1.3 spec, `vkAllocateCommandBuffers` 可返回):

| VkResult | 含义 | 修复方向 |
|----------|------|----------|
| `VK_ERROR_OUT_OF_HOST_MEMORY` | 主机内存不足 | 检查是否有资源泄漏 |
| `VK_ERROR_OUT_OF_DEVICE_MEMORY` | GPU 内存不足 | 检查已分配资源量 |
| 空 vector (无异常) | 驱动异常行为 | pool 已创建但分配失败，需深度排查 driver 或 pool 生命周期 |

注：`VK_ERROR_INITIALIZATION_FAILED` 和 `VK_ERROR_OUT_OF_POOL_MEMORY` 不在 `vkAllocateCommandBuffers` 的规范错误码中（对抗审查确认），已从决策表中移除。

### D2: 首次帧跳过 resetCommandPool

**选择**: 在 `VulkanFrameContext::Aquire()` 中，将 `resourceManager.Reset()` 从无条件调用移到 `if (!m_FirstFrame)` 分支内，使首次帧跳过对从未分配过的 pool 的 reset。

**理由**: 
- 首次帧所有状态已经是默认值：`m_GraphicsCommand = nullptr`（Init 未设置）、fence 未提交、semaphore index 为 0
- `vkResetCommandPool` 不是幂等操作的保证——某些驱动在从未分配过的 pool 上调用 reset 可能产生未预期行为
- 这个调用路径恰好解释了观察到的症状："pool 创建成功 → reset → 分配失败"
- 改动 3 行代码，零风险

**替代方案**: 在 `VulkanCommandListManager::Reset()` 中添加保护（如检查是否从未分配过）→ **拒绝**，因为会影响后续帧的合法 reset 流程。

### D3: 对抗审查排除的假设

经过 4 维度 × 23 初步发现 × 8 对抗验证的审查，以下假设被排除：

**已排除 — Device 一致性检查**: 所有 VulkanSubobjectBase 实例通过 `SetApp()` 共享同一个 `RenderBackend_Vulkan*`，`GetDevice()` 始终返回唯一的 `m_Device`。不存在跨 Device 使用 pool 的可能性。无需添加 Device 句柄比对断言。

**已排除 — Queue Family 检查**: 若 queue family index 不正确，`createCommandPool` 就会失败（抛异常）。全局 pool 能创建成功 → queue family index 有效。且 `vkAllocateCommandBuffers` 不返回 `VK_ERROR_INITIALIZATION_FAILED`。

**已排除 — "双重初始化"**: 两条初始化路径等价——均使用零参 `InitSubObj`（仅 `SetApp`）+ 显式 `Init()`。C++ 模板重载偏向非变参版本。

---

## Risks / Trade-offs

| 风险 | 缓解措施 |
|------|----------|
| 添加诊断后仍无法定位根因 | 在 `VulkanFrameBoundResourceManager::Init` 末尾添加 pool 有效性验证（尝试分配后立即释放） |
| 修复可能涉及 frame manager 重构 | 将修复范围限定在当前命中的问题，不扩大 scope |
| `TestSimpleTriangle` 通过但更复杂测试依然失败 | 这是预期内的——一个 bug 一个 proposal |

## 对抗审查发现的新问题

### Release() 关闭顺序违反 Vulkan 规范

`RenderBackend_Vulkan::Release()` 在调用 `m_Device.waitIdle()` 之前就销毁了 FrameContext 的 pool/fence/semaphore：

```
line 296: m_GPUFrameManager.Release()  // 销毁所有 pool、fence、semaphore
  ...
line 374: m_Device.waitIdle()          // 此时 GPU 可能仍在执行最后一帧
```

`VulkanGPUFrameManager::WaitIdle()` 已实现（VulkanFrameManager.cpp:270）但从未被调用。正常的 Aquire() 流程会在下一帧等待 fence，但关闭时没有"下一帧"来触发等待。

**修复**: 在 `m_GPUFrameManager.Release()` 前调用 `m_GPUFrameManager.WaitIdle()`。

---

## Open Questions

1. **为什么全局 `VulkanCommandListManager` 能工作但 FrameContext 里的不能？** — 两条路径初始化代码等价，需运行时诊断确认差异点
2. **"Internal buffer not registered" 警告是否相关？** — 这个警告来自资源管理系统，理论上不应影响命令缓冲区分配，但需要确认是否有间接影响
