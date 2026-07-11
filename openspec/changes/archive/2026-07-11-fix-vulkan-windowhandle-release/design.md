## Context

`VulkanWindowHandle` 继承自 `WindowHandle` 和 `VulkanSubobjectBase`。其析构函数当前为 `= default`（编译器生成），不执行任何 Vulkan 资源清理。

Vulkan 资源（`vk::SwapchainKHR`、`vk::SurfaceKHR`、`vk::ImageView`）是整数句柄，不具备 RAII 语义。`Release()` 方法包含正确的清理逻辑（`CleanupSwapchain()` + `destroySurfaceKHR()`），但仅在显式调用时执行。

D3D12 后端使用 `ComPtr<T>`（COM 智能指针），析构时自动调用 `Release()`，天然具备 RAII 语义，无需额外的清理方法。

当前 Vulkan 后端的 `WindowHandle` 生命周期：
```
GetWindowHandle() → shared_ptr<VulkanWindowHandle> → 测试结束时 ref count=0
→ ~VulkanWindowHandle() = default → Vulkan 资源句柄丢失（未调用 vkDestroy*）
→ 模块卸载时 vkDestroyDevice/vkDestroyInstance 隐式清理 → 部分驱动崩溃
```

此外，`VulkanSubobjectBase::GetApp()` 返回裸指针 `pApp`，该指针由 `SetApp()` 设置后**永不被清除为 nullptr**。若 `RenderBackend_Vulkan` 先于子对象析构，`pApp` 成为 dangling pointer（非空），任何通过 `GetDevice()`/`GetInstance()` 的访问都是 use-after-free。

## Goals / Non-Goals

**Goals:**
- `VulkanWindowHandle` 析构时自动释放所有 Vulkan 资源（swapchain、image views、surface）
- `Release()` 支持重复调用（幂等），兼容显式调用和析构函数两路调用
- 保证 `VulkanWindowHandle` 的 Vulkan 资源在 `RenderBackend_Vulkan::Release()` 销毁 device/instance **之前**释放
- 与 D3D12 `WindowContext` 的 RAII 行为对齐

**Non-Goals:**
- 不改变 `VulkanWindowHandle` 的公开接口（析构函数声明变更除外）
- 不改变其他 Vulkan 子对象的清理模式（本次只修 WindowHandle）
- 不引入 `VulkanSubobjectBase` 生命周期感知机制（长期架构改进，独立变更）

## Decisions

### 决策 1：双层释放策略

```
RenderBackend_Vulkan::Release() 主动释放:
  for (auto& [w, weakHandle] : m_WindowHandles)
      if (auto h = weakHandle.lock()) h->Release();   ← 此时 device/instance 仍有效
  m_WindowHandles.clear();
  // ... 之后才执行 device.destroy() / instance.destroy()

VulkanWindowHandle::~VulkanWindowHandle():
  Release();   ← 若步骤1已释放 → m_Released=true → no-op
               ← 若步骤1未覆盖（对象在 map clear 之后创建）→ 此时释放
```

两层保证：
1. **第一层**：`RenderBackend_Vulkan::Release()` 主动遍历释放所有已知 WindowHandle → 正常的清理路径
2. **第二层**：析构函数兜底 → 覆盖异常路径（如 WindowHandle 在 `m_WindowHandles.clear()` 之后才被创建）

### 决策 2：Release() 幂等保护 — `m_Released` flag

```cpp
void VulkanWindowHandle::Release()
{
    if (m_Released) return;
    m_Released = true;
    
    CleanupSwapchain();                        // destroy image views + swapchain
    if (m_Surface) {
        GetInstance().destroySurfaceKHR(m_Surface);
        m_Surface = nullptr;
    }
    m_Window.reset();
}
```

`CleanupSwapchain()` 已具备部分幂等性（检查 `m_Swapchain` 后 destroy，然后设 nullptr）。`m_Surface` 首次 destroy 后设为 nullptr。`m_Released` flag 提供顶层的快速返回，避免重复执行整个函数体。

注意：若 `m_Released` 被用于 `RecreateSwapchain()` 场景（`Release()` 后 `RecreateSwapchain()` 重建资源→析构时需再次清理），`RecreateSwapchain()` 应在成功重建后重置 `m_Released = false`。当前代码不会出现此场景（`Release()` 只在析构或后端关闭时调用），暂不处理。详见 Risks。

### 决策 3：删除移动构造函数

`VulkanWindowHandle` 的移动构造函数当前为 `= default`。`vk::SwapchainKHR` / `vk::SurfaceKHR` 是 uint64_t 的包装，默认移动只是拷贝整数，源对象句柄不变。添加 RAII 析构后，若两个对象持有相同 Vulkan 句柄，会重复 vkDestroy。

```cpp
// 从:
VulkanWindowHandle(VulkanWindowHandle&& other) noexcept = default;
VulkanWindowHandle& operator=(VulkanWindowHandle&& other) noexcept = default;

// 改为:
VulkanWindowHandle(VulkanWindowHandle&&) = delete;
VulkanWindowHandle& operator=(VulkanWindowHandle&&) = delete;
```

`VulkanWindowHandle` 始终通过 `shared_ptr<WindowHandle>` 持有，不需要移动语义。D3D12 `WindowContext` 同样无移动构造函数。

### 决策 4：析构函数实现位置

析构函数声明在 header（替换 `= default`），实现放在 `.cpp` 文件中。理由：避免 header 中包含 `Release()` 的完整逻辑（保持编译隔离）。

## Risks / Trade-offs

- **重复释放风险**：调用方同时显式 `Release()` + 析构函数 → `m_Released` flag 缓解。`RenderBackend_Vulkan::Release()` 主动释放 + 析构函数兜底的双层策略进一步降低了重复调用的风险（第一层在 device 有效时完成，第二层大概率是 no-op）
- **`m_Released` 与 `RecreateSwapchain()` 交互**：若未来有 `Release()` 后调用 `RecreateSwapchain()` 的代码路径，需在重建后重置 `m_Released = false`。当前无此路径，暂不处理。可在 `RecreateSwapchain()` 末尾添加 `m_Released = false` 作为预防措施（包含在 tasks 中）
- **`m_Released` 是新模式**：现有代码通过成员级空检查实现部分幂等性（`if (m_Surface)`），无集中式 `m_Released` flag。此 flag 的引入理由充分（析构函数 + 显式 Release 两条路径），设计文档已记录此决策
- **移动构造删除**：理论上不会破坏现有代码（`VulkanWindowHandle` 始终通过 `shared_ptr` 管理）。若未来有代码试图 move，编译器会明确报错
