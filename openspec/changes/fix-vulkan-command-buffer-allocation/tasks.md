# Tasks: 修复 Vulkan 命令缓冲区分配失败

**Change ID**: fix-vulkan-command-buffer-allocation
**Updated**: 2026-07-15（对抗审查修订 v2 — 纳入首次帧 Reset 跳过）

---

## 1. 首次帧跳过 Reset（低风险预防性修复 — 可能就是根因）

- [x] 1.1 在 `VulkanFrameContext::Aquire()` 中，将 `resourceManager.Reset()` 从无条件调用移到 `if (!m_FirstFrame)` 分支内。

  当前代码（VulkanFrameManager.cpp:208-217）：
  ```cpp
  if (!m_FirstFrame)
  {
      // ... wait for fences ...
      resourceManager.ResetDescriptorPool();
  }
  else
  {
      m_FirstFrame = false;
      resourceManager.CreateFences();
  }

  resourceManager.Reset();  // ← 首次帧也对未使用过的 pool 调 resetCommandPool
  ```

  修复后：
  ```cpp
  if (!m_FirstFrame)
  {
      // ... wait for fences ...
      resourceManager.ResetDescriptorPool();
      resourceManager.Reset();  // ← 仅非首次帧需要 reset
  }
  else
  {
      m_FirstFrame = false;
      resourceManager.CreateFences();
      // 首次帧所有状态已是默认值，无需 Reset
  }
  ```

  > 理由：对抗审查发现某些 GPU 驱动可能在从未分配过的 pool 上调用 `vkResetCommandPool` 后行为异常。首次帧所有状态（`m_GraphicsCommand = nullptr`、fence 未提交、semaphore index = 0）是 Init 后的默认值，跳过 Reset 安全且零成本。如果此修复解决了崩溃，则无需后续诊断步骤。

- [x] 1.2 运行 `build.py` 编译，运行 `GPUBackendTester --backend vulkan --test TestSimpleTriangle --headless 5 --headless-timeout 30` 验证是否修复。（结论：首次帧 Reset 跳过未解决崩溃，排除此假设）

## 2. 诊断增强（若步骤 1 未解决）

- [x] 2.1 在 `VulkanCommandListManager::AllocateCommandBuffer` 中添加 try-catch 包裹 `allocateCommandBuffers`（本项目 vulkan.hpp 为 throwing 模式，返回 `std::vector<vk::CommandBuffer>` 而非 `ResultValue`）。捕获 `vk::SystemError` 异常时用 `CA_LOG_ERR` 输出 `e.what()` 和 pool 原始句柄。空 vector（无异常）时也输出 pool 句柄。参考代码：

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
              CA_LOG_ERR("VulkanCommandListManager: allocateCommandBuffers returned empty vector, pool=0x{:x}",
                  static_cast<VkCommandPool>(pool));
              CA_LOG_ERR("  GraphicsPool=0x{:x}, ComputePool=0x{:x}, TransferPool=0x{:x}",
                  static_cast<VkCommandPool>(m_GraphicsPool),
                  static_cast<VkCommandPool>(m_ComputePool),
                  static_cast<VkCommandPool>(m_TransferPool));
          }
          VK_RESULT_CHECK(!cmdBufs.empty() ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY);
          return cmdBufs[0];
      }
      catch (vk::SystemError const& e)
      {
          CA_LOG_ERR("VulkanCommandListManager: allocateCommandBuffers threw exception: {} (pool=0x{:x})",
              e.what(), static_cast<VkCommandPool>(pool));
          throw;
      }
  }
  ```

- [x] 2.2 在 `VulkanCommandListManager::Init` 中，pool 创建成功后添加 `CA_LOG_INFO` 输出每个 pool 的原始 `VkCommandPool` 句柄值及对应的 `queueFamilyIndex`。

  > 注：对抗审查确认所有 subobject 共享同一 `RenderBackend_Vulkan*`，无需 Device 句柄一致性断言（死代码）。

- [ ] 2.3 运行 `build.py` 编译，运行测试获取实际错误信息。

## 3. 根因修复（基于诊断结果）

- [ ] 3.1 根据步骤 2.3 获取的诊断信息，实施对应修复。常见场景：

  | 诊断结果 | 修复方向 |
  |----------|----------|
  | `vk::SystemError` 异常（`VK_ERROR_OUT_OF_HOST_MEMORY`） | 检查是否有资源泄漏 |
  | `vk::SystemError` 异常（`VK_ERROR_OUT_OF_DEVICE_MEMORY`） | 检查已分配 GPU 资源量 |
  | 空 vector 无异常 | 驱动异常行为。验证 pool handle 有效性 |

  > 注：`VK_ERROR_INITIALIZATION_FAILED` 和 `VK_ERROR_OUT_OF_POOL_MEMORY` 不在 `vkAllocateCommandBuffers` 规范错误码中（对抗审查确认），已移除。

- [ ] 3.2 修复后重新编译运行测试，确认 `TestSimpleTriangle` 不再崩溃。

## 4. Release() 关闭顺序修复

- [x] 4.1 在 `RenderBackend_Vulkan::Release()` 中，`m_GPUFrameManager.Release()` 之前添加 `m_GPUFrameManager.WaitIdle()` 调用。`VulkanGPUFrameManager::WaitIdle()` 已实现（VulkanFrameManager.cpp:270），直接调用。

  修复后：
  ```cpp
  m_GPUFrameManager.WaitIdle();   // 等 GPU 完成
  m_GPUFrameManager.Release();    // 再安全销毁
  ```

  > 对抗审查发现。虽不导致首次帧崩溃，但违反 Vulkan 规范。

## 5. "Internal buffer not registered" 警告排查（可选）

- [ ] 5.1 若步骤 3 修复后测试仍不通过（不再崩溃但渲染异常），排查 `VulkanGraphLocalResourceManager::GetBuffer` 中警告的根因。

## 6. 编译验证与测试

- [ ] 6.1 运行 `build.py`，确保 BUILD SUCCESSFUL。
- [ ] 6.2 运行 `GPUBackendTester --backend vulkan --test TestSimpleTriangle --headless 10 --headless-timeout 60`，确认不再崩溃且 validation log 为空。
