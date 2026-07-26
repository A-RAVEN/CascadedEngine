## Context

`VulkanCommandListManager.cpp` 中 4 处 `CA_LOG_INFO`/`CA_LOG_ERR` 调用使用了 `static_cast<void*>(static_cast<VkCommandPool>(...))` 参数。CACore 日志宏的第一个参数类型是 `fmt::format_string<T...>`，触发 fmt v11 的编译期格式字符串检查（`fstring` 构造函数的 `consteval` 检查）。`void*` 类型在 MSVC + fmt v11 的 `consteval` 环境下通不过编译期检查器，导致 `report_error()` 被调用（非 `constexpr` 函数）→ `error C3615`。

## Goals / Non-Goals

**Goals:**
- 修复 4 处 fmt 编译错误，使 `VulkanCommandListManager.cpp` 通过编译
- 保持日志输出的信息量不变（仍然能看到 pool handle 的数值）

**Non-Goals:**
- 不修改 CACore 日志宏
- 不修改 fmt 库
- 不影响 `fix-vulkan-command-buffer-allocation` 中其他文件的改动

## Decisions

### D1: 将 `void*` 替换为 `uintptr_t`

- **选择**：`reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(pool))`
- **备选**：
  - `fmt::ptr(p)` → 返回 `const void*`，仍然是编译期检查器不接受指针类型
  - 提取到局部 `void*` 变量 → 模板参数仍推导为 `void*`，问题不变
  - 用 `fprintf`/`printf` 绕过 fmt → 引入不一致的日志风格，且不经过 CACore 日志系统
- **理由**：`uintptr_t` 是整数类型，fmt 对整数的编译期支持最成熟稳定

### D2: 日志输出格式

- **选择**：保持 `{}` 格式，不做十六进制格式化（`{:x}` 也会被检查器拒绝）
- **理由**：`{}` 对整数的支持最稳定，输出为十进制可读地址值

## Risks / Trade-offs

- [轻微] 日志输出从十六进制指针（如 `0x1a2b3c4d`）变为十进制整数（如 `439041101`），可读性略有下降。需要 `0x` 前缀的话可以在代码中手动格式化。
