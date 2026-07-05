# Design: 修复 Vulkan 子对象 App 指针未传播导致的崩溃

**Change ID**: fix-vulkan-subobject-app-pointer
**Created**: 2026-07-05

---

## Context

`VulkanGraphExecutor` 是 `VulkanSubobjectBase` 的子类，其内部包含多个同样是 `VulkanSubobjectBase` 子类的成员对象：

```
VulkanGraphExecutor : VulkanSubobjectBase
  ├─ m_LocalResourceManager : VulkanGraphLocalResourceManager : VulkanSubobjectBase
  │    └─ m_AliasingManager : VulkanResourceAliasing : VulkanSubobjectBase
  └─ m_ConstantBufferManager : VulkanConstantBufferManager : VulkanSubobjectBase
```

`VulkanSubobjectBase` 通过 `pApp` 指针持有 `RenderBackend_Vulkan*`，所有访问后端设施的方法（`GetDevice()`、`GetMemoryManager()` 等）都通过 `pApp` 间接调用。

**当前问题**：`RenderBackend_Vulkan::ExecuteGraph` 仅为栈上的 `VulkanGraphExecutor` 设置了 `pApp`（通过 `InitSubObj`），但未传播给其内部成员。所有嵌套子对象的 `pApp` 处于未初始化状态。

**约束**：`SetApp` 是 `private`，仅 `friend class RenderBackend_Vulkan` 可访问。任何外部代码无法直接设置子对象的 `pApp`。

## Goals / Non-Goals

**Goals:**
- 确保 `VulkanGraphExecutor` 所有嵌套的 `VulkanSubobjectBase` 子对象在调用 `GetApp()` 前已获得有效的 `pApp`
- 使 `pApp` 未初始化时的行为可预测（nullptr 解引用）

**Non-Goals:**
- 不改变 `InitSubObj` 的模板设计
- 不引入递归自动传播机制
- 不修改其他 Vulkan 子对象类型

## Decisions

### D1: 手动逐层传播而非自动递归

**选择**: 在每个需要传播的层级显式调用 `GetApp()->InitSubObj(&child)`。

```
ExecuteGraph()
  InitSubObj(&executor)           ← 已存在
  
CompileAndExecute()
  GetApp()->InitSubObj(&m_LRM)    ← 新增 (D1a)
  GetApp()->InitSubObj(&m_CBM)    ← 新增 (D1b)

AllocateAliasedResources()
  GetApp()->InitSubObj(&m_AM)     ← 新增 (D1c)
```

**原理**: 改动最小（3 行），不引入机制变更。`GetApp()` 返回 `RenderBackend_Vulkan*`，而 `InitSubObj` 是 `RenderBackend_Vulkan` 的模板方法，可通过 friendship 访问 `SetApp`。

**备选方案**: 在 `VulkanSubobjectBase` 中添加 `virtual void OnAppSet()` 回调或自动递归 → 拒绝，过度设计，只有 3 个受影响的子对象。

### D2: `InitSubObj` 无参重载（仅 SetApp，不调用 Init）

**选择**: 使用无参版本的 `InitSubObj`，其实现为：

```cpp
template<typename T>
void InitSubObj(T* inoutObj)
{
    static_assert(DerivedFrom<T, VulkanSubobjectBase>);
    if constexpr (DerivedFrom<T, VulkanSubobjectBase>)
        static_cast<VulkanSubobjectBase*>(inoutObj)->SetApp(this);
}
```

**原理**: 传播 `pApp` 时无需重新初始化对象 (subobject 的数据已通过其他路径填充：`RegisterTemporary*` 等方法)。`SetApp` + `Init` 的完整流程仅在 `ExecuteGraph` 首次创建 executor 时需要。

### D3: `pApp` 显式初始化为 `nullptr`

**选择**: 在 `VulkanSubobjectBase` 中：

```cpp
RenderBackend_Vulkan* pApp = nullptr;
```

替代当前的无初始化声明 `RenderBackend_Vulkan* pApp;`。

**原理**: 未初始化的指针导致崩溃时的 this 地址是随机值（如 `0x1dc`），增加调试难度。初始化为 `nullptr` 后，崩溃是标准的 null deref（地址 0 附近），更容易通过调试器定位。

## Risks / Trade-offs

- **[传播遗漏风险]**: 如果未来新增 VulkanGraphExecutor 嵌套子对象，可能忘记传播 `pApp`。
  → 缓解：当前已知 2 个直接子对象（`m_LocalResourceManager`、`m_ConstantBufferManager`），均已修复。`VulkanGraphLocalResourceManager` 内的 `m_AliasingManager` 也已修复。

- **[InitSubObj 副作用]**: 无参 `InitSubObj` 只调用 `SetApp`，不调用 `Init()`。子对象可能依赖 `Init()` 中的初始化逻辑。
  → 缓解：`VulkanGraphExecutor` 及其子对象是每帧新构造的栈对象，成员容器默认构造后为空，`RegisterTemporary*` 方法在后续填充数据。不调用 `Init()` 不会丢失状态。

