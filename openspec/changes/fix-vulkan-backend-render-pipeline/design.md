# Design: 修复 Vulkan 后端渲染管线关键缺口

**Change ID**: fix-vulkan-backend-render-pipeline
**Updated**: 2026-06-28（四轮审查后：GPL+VK_NULL_HANDLE 经 spec 验证正确，聚焦非 GPL else 分支修复）

---

## Context

源码级审查揭示 6 类实际缺口（经过四轮对抗验证，排除了不存在 API 字段的 Indirect、无调用方的 SubGraph）：

| # | 缺口 | 位置 | 类型 |
|---|------|------|------|
| 1 | Buffer::UploadData GPU-only 路径静默失败 | `VulkanBuffer.cpp:127` | CRITICAL |
| 2 | Texture::UploadData 完全空实现 | `VulkanTexture.cpp:219` | CRITICAL |
| 3 | PipelineStates 结构体为空占位 | `FragmentOutputStates.h` 等 3 文件 | MEDIUM |
| 4 | StateManager 复制粘贴残留（内外两层类名错误） | `FragmentOutputStateManager.h`, `GeometryShaderStateManager.h` | MEDIUM |
| 5 | CreateMonolithicPipeline 7 个 pipeline state 值硬编码 | `VulkanGraphExecutor.cpp:1360-1381` | MEDIUM |
| 6 | 非 GPL 路径 Pipeline 创建空实现——`CreateMonolithicPipeline` 仅设 layout，GPL 路径 `VK_NULL_HANDLE` 符合 spec | `VulkanGraphExecutor.cpp:1400-1405` | CRITICAL |

**已排除**:
- Indirect Draw/Dispatch：`DrawInfo` 和 `ComputeDispatch` 缺少 `drawIndirect`/`indirectArgsBuffer` 字段——独立接口层变更
- SubGraph 执行：`eSubGraph` 从未被任何后端实现，无调用方，GPUGraph.h 死代码由 Task 4.x 清理

## Goals / Non-Goals

**Goals:**
- 打通 Buffer 和 Texture 的 CPU→GPU 数据上传（device-local 内存场景）
- Pipeline States 结构体和 StateManager 达到可用状态
- `CreateMonolithicPipeline` 中 7 个硬编码值从 Pass/DrawCallBatch 数据动态读取
- 非 GPL 路径 Pipeline 创建补齐（GPL 路径 `VK_NULL_HANDLE` 经 spec 验证正确，无需修改）
- 清理 `GPUGraph.h` 中从未实现的 `eSubGraph` 死代码

**Non-Goals:**
- 接口签名修改（已验证正确）
- ExecuteGraph 连接（已有完整实现）
- Indirect Draw/Dispatch（`DrawInfo`/`ComputeDispatch` 缺少必要字段——独立接口层变更）
- SubGraph 执行（从未实现，已清理声明）
- Ray Tracing / Mesh Shader / DescriptorSet 路径优化 / Linux Surface / PipelineLibrary 优化

---

## Decisions

### D1: Buffer::UploadData Staging 路径

**选择**: 在 `VulkanBuffer::UploadData` 内创建临时 staging buffer → `vkCmdCopyBuffer` → 立即提交并等待

**原理**: 与 D3D12 的 upload heap → default heap 复制模式一致。Vulkan 端已有 `LinearMemoryManager` 用于帧级 staging 分配，但 UploadData 是单次同步上传，应使用独立 staging buffer 以确保数据立即可见。

**触发条件**: 仅当 `Map()` 返回 `nullptr` 时走 staging 路径。当 VMA 分配了 persistently mapped 内存（`VMA_ALLOCATION_CREATE_MAPPED_BIT`）时，`m_MappedPtr` 总是有效，直接走 memcpy 路径。

```
UploadData(pData, size, offset)
  ├─ 检查是否 CPU 可映射 → 直接 memcpy + Unmap（已有）
  └─ GPU-only（Map() 返回 null）:
       ├─ 创建 staging buffer（VK_BUFFER_USAGE_TRANSFER_SRC | VK_MEMORY_PROPERTY_HOST_VISIBLE | HOST_COHERENT，size = size 参数）
       ├─ memcpy 到 staging buffer
       ├─ 获取命令缓冲（从 VulkanFrameManager 或 CommandListManager 获取）
       ├─ vkCmdCopyBuffer(staging → target, region={srcOffset=0, dstOffset=offset, size})
       ├─ 添加 pipeline barrier：src=TRANSFER_WRITE → dst=目标 buffer 的实际 usage（VERTEX|INDEX|UNIFORM|STORAGE|INDIRECT）
       ├─ Submit + WaitIdle（同步上传，保证调用返回后数据可用）
       └─ 销毁 staging buffer
```

**备选方案**: 使用 `LinearMemoryManager::Allocate` → 被拒绝，因为 LinearMemoryManager 是帧级分配器，UploadData 是独立 API 调用，生命周期不匹配。

### D2: Texture::UploadData Staging 路径

**选择**: Staging buffer → `vkCmdCopyBufferToImage` → 布局恢复

**原理**: VulkanTexture 已有 `TransitionLayout` 辅助函数可复用。需要为每个 mip level 和 array layer 构造独立的 `VkBufferImageCopy` 结构。

**关键细节**:
- Staging buffer 大小 = 全纹理数据（所有 mip/layer 的紧密打包字节数）
- 对于紧密打包数据（rowLength=0, imageHeight=0），Vulkan 自动从 image extent 推导
- 逐 mip level 循环：每个 level 需要独立的 `VkBufferImageCopy`，指定 `bufferOffset`（数据起始位置）、`imageSubresource.mipLevel`、`imageExtent`
- D24_UNORM_S8_UINT 等特殊格式的 bytes-per-pixel 需单独计算

```
UploadData(pData, size)
  ├─ 计算 staging buffer size（全 mip/layer 紧密打包大小）
  ├─ 创建 staging buffer（VK_BUFFER_USAGE_TRANSFER_SRC | HOST_VISIBLE | HOST_COHERENT）
  ├─ memcpy 到 staging buffer
  ├─ 记录当前布局（m_CurrentLayout）
  ├─ TransitionLayout: 当前 → VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
  ├─ 构建 castl::vector<VkBufferImageCopy> regions（每 mip level + array layer 一个）
  ├─ vkCmdCopyBufferToImage(staging → target image, regions)
  ├─ TransitionLayout: TRANSFER_DST_OPTIMAL → 原始布局
  ├─ Submit + WaitIdle
  └─ 销毁 staging buffer
```

**备选方案**: 异步上传 → 被拒绝，UploadData 语义是同步的（调用返回后数据必须可用）。

### D3: Pipeline States 结构体完整化

**选择**: 参考 D3D12 对应结构体，填入 Vulkan GPL 所需的字段

**FragmentOutputStates**: 需要 color attachment formats、depth/stencil format、multisample state。当前是空结构体 `{}`。
```cpp
struct FragmentOutputStateCache {
    castl::vector<vk::Format> colorFormats;  // 颜色附件格式
    vk::Format depthStencilFormat;           // 深度模板格式
    vk::SampleCountFlagBits sampleCount;     // MSAA 采样数
    // hash/equality operators for cache key
};
```

**FragmentShaderStates**: 当前是空命名空间 `namespace graphics_backend { }`（不是空结构体），需创建 `FragmentShaderStateCache` 结构体。grep 确认无外部源文件引用该命名空间，改写安全。
```cpp
struct FragmentShaderStateCache {
    TypedVKHashVal<ShaderModuleCache> fragmentShaderCache;
};
```

**GeometryShaderStates**: 当前仅有 `vertexShaderCache`，需补齐 tess/geom。
```cpp
struct GeometryShaderStates {
    TypedVKHashVal<ShaderModuleCache> vertexShaderCache;
    TypedVKHashVal<ShaderModuleCache> tessControlShaderCache;
    TypedVKHashVal<ShaderModuleCache> tessEvalShaderCache;
    TypedVKHashVal<ShaderModuleCache> geometryShaderCache;
};
```

### D4: StateManager 修正

**选择**: 将 `FragmentOutputStateManager.h` 和 `GeometryShaderStateManager.h` 中的类名、内部类名、方法名和类型引用全部修正

**当前问题**: 两个头文件完全复制自 `VertexInputStateManager.h`，包含两层错误：
- **外层类名**: `VertexInputStateManager`（应为 `FragmentOutputStateManager` / `GeometryShaderStateManager`）
- **内层类名**: `FragmentOutputState`（在 GeometryShaderStateManager.h 中也叫这个——应该是 `GeometryShaderState`）
- **内层类的成员类型**: `FragmentOutputStateCache m_StateCache`（Geometry 版本应为 `GeometryShaderStates m_StateCache`）
- **方法名**: `EnsureFragmentOutputState`（两个文件都叫这个）/ `GetVertexInputState`（名字完全不匹配）

**修正方案**:
- `FragmentOutputStateManager.h`: 外部类名 → `FragmentOutputStateManager`，内部类名 → `FragmentOutputState`（保留，语义正确），方法 → `EnsureFragmentOutputState` / `GetFragmentOutputState`，使用 `FragmentOutputStateCache`
- `GeometryShaderStateManager.h`: 外部类名 → `GeometryShaderStateManager`，内部类名 → `GeometryShaderState`（修正），内部类成员类型 → `GeometryShaderStates`（修正），方法 → `EnsureGeometryShaderState` / `GetGeometryShaderState`，使用 `GeometryShaderStates`

**类型系统验证**: `TypedVKHashVal<T>` 使用 `cacore::hash_256<T>{}` 计算哈希。`<vulkan/vulkan_hash.hpp>` 为 `vk::Format`、`vk::SampleCountFlagBits` 提供 hash 支持。`VKHashFunc<T>(obj)` 可为任意可哈希类型创建 TypedVKHashVal。所有新增字段的类型均可哈希。

### D5: CreateMonolithicPipeline 硬编码值动态化

**选择**: 将 `CreateMonolithicPipeline` 中 7 个硬编码的 pipeline state 值从 Pass/DrawCallBatch 数据动态读取

**数据可用性验证**: `HashObj<T>` 存储原始值并提供访问（`.Get()` 或隐式转换）。`InputAssemblyStates`（含 `topology`）和 `CPipelineStateObject`（含 `rasterizationStates`、`colorAttachments`、`msCount`）均为具体结构体，数据可读取。

**重要**: `viewportCount = 1` 和 `scissorCount = 1` **不是 bug**。Vulkan 中 viewport/scissor 始终是 dynamic state——pipeline 中的 count 声明支持的最大数量，实际值通过 `vkCmdSetViewport`/`vkCmdSetScissor` 在录制时动态设置（代码 line 1747 已做）。保持 1 是标准单-viewport 用法，无需修改。

| 硬编码值 | 当前值 | 数据来源 | 是否修复 |
|---------|--------|---------|---------|
| `inputAssemblyState.topology` | `eTriangleList` | `batch.pipelineStateDesc.m_InputAssemblyStates.Get().topology` | ✅ |
| `rasterizationState.polygonMode` | `eFill` | `batch.pipelineStateDesc.m_PipelineStates.Get().rasterizationStates.polygonMode` | ✅ |
| `rasterizationState.cullMode` | `eBack` | `.rasterizationStates.cullMode` | ✅ |
| `rasterizationState.frontFace` | `eClockwise` | `.rasterizationStates.frontFace` | ✅ |
| `multisampleState.rasterizationSamples` | `e1` | `.msCount`（enum → vk::SampleCountFlagBits） | ✅ |
| `colorBlendAttachment.colorWriteMask` | RGBA 全通道 | `.colorAttachments.attachmentBlendStates[i].channelMask` | ✅ |
| `colorBlendState.attachmentCount` | `1` | `rasterPass.GetColorAttachmentCount()` ⚠️ `attachmentBlendStates.size()` 是定长数组永远返回 8，不可用 | ✅ |
| `viewportState.viewportCount` | `1` | 保持（dynamic viewport 标准用法） | ❌ 无需改 |
| `viewportState.scissorCount` | `1` | 保持（同上） | ❌ 无需改 |

### D6: 非 GPL 路径 Pipeline 创建补齐

**选择**: 修复 `CreateMonolithicPipeline` 的 else 分支（line 1400-1405），GPL 路径保持不变

**原理**: 
- **GPL 路径 `VK_NULL_HANDLE` 是正确的**：经 Vulkan spec 验证（VUID-VkGraphicsPipelineCreateInfo-flags-06579），四个 GPL library part 均创建为 render-pass-independent（默认 `VK_NULL_HANDLE`）时，LinkPipeline 同样传 `VK_NULL_HANDLE` 产生的 pipeline 兼容**任意** `VkRenderPass`。录制端 `RecordRenderPass` 使用真实 render pass，绑定此 pipeline 是合法操作。GPL 路径无需任何修改。
- **非 GPL else 分支是坏的**：当前仅设置 `createInfo.layout = pipelineLayout`，缺少 shader stages、render pass、vertex input、viewport、rasterization、multisample、color blend 等必要字段。需将这些状态（已在 `if` 块内构建）移出 `if` 块使其在 else 分支可访问，填入完整的 `VkGraphicsPipelineCreateInfo` 并传入 `GetOrCreateRenderPass` 获取的真实 render pass。

**工作量**: 将 7 个 state struct 的声明从 `if` 块内移至 `if/else` 之前；else 分支组装 `VkGraphicsPipelineCreateInfo`。

### D7: GPUGraph.h 死代码清理

**选择**: 移除 `EGraphStageType::eSubGraph` 枚举值、`SubGraph()` 方法、`m_SubGraphs` 成员

**原理**: 这些声明从未被任何后端（D3D12、Vulkan、VulkanNew）实现，也无任何应用代码或测试调用。清除后不影响任何现有功能，同时消除将来误用的风险。

---

## Risks / Trade-offs

- **[UploadData 同步等待]**：使用 `Submit + WaitIdle` 实现同步上传，性能不是最优（GPU 空闲等待），但 UploadData API 语义是同步的，调用方期望返回后数据可用。帧内的批量上传由 TransferPass + `LinearMemoryManager` + 异步 Submit 处理。
  → 缓解：在实现注释中标注同步上传的性质，未来可优化为延迟提交。

- **[Texture UploadData mip/layer 复杂度]**：纹理上传需要为每个 mip level 和 array layer 构建独立的 `VkBufferImageCopy`。对于深度模板格式需特殊处理 bytes-per-pixel 计算。
  → 缓解：参考 VulkanTexture 已有的 `ConvertFormat` 和 `GetImageAspect` 辅助函数扩展。

- **[StateManager 两层类名修正]**：修正 FragmentOutputStateManager 和 GeometryShaderStateManager 时，不仅要改外层类名，还要改内层 `FragmentOutputState` 类名（几何着色器版本）及其成员类型。修正后需确保所有使用站点重新编译。
  → 缓解：CMake 自动处理依赖。在实现前用 grep 确认所有引用点。

- **[FragmentShaderStates 空命名空间 → 结构体]**：当前是 `namespace graphics_backend { }`，需改为声明 `FragmentShaderStateCache` 结构体。grep 已确认无外部引用，改写安全。
  → 缓解：若后续发现未知引用导致编译错误，单独修复。

- **[GPUGraph.h 枚举值移除]**：删除 `eSubGraph` 后，若任何代码引用了该枚举值会编译失败。grep 确认仅在 `SubGraph()` 方法内部使用，该方法同步删除。VulkanGraphExecutor 的 switch 从未包含此 case。
  → 缓解：编译验证即可确认无遗漏引用。

- **[缺少 enum 转换函数]**：US3 所有动态读取任务需要将引擎层枚举转换为 Vulkan 枚举（`ETopology→VkPrimitiveTopology`、`EPolygonMode→VkPolygonMode`、`ECullMode→VkCullModeFlags`、`EFrontFace→VkFrontFace`、`EColorChannelMask→VkColorComponentFlags`、`EMultiSampleCount→VkSampleCountFlagBits`）。新后端当前仅有 `VulkanTexture::ConvertSampleCount`。旧后端 `InterfaceTranslator.h` 有完整实现可参考。
  → 缓解：Task 3.7 明确创建这些转换函数。可复用旧后端代码。

- **[`attachmentBlendStates.size()` 陷阱]**：`castl::array<T,8>` 是定长数组，`.size()` 永远返回 8。Vulkan 要求 colorBlend attachment count 等于 subpass 颜色附件数，否则校验失败。正确来源是 `rasterPass.GetColorAttachmentCount()`。
  → 缓解：Task 3.5 已修正为正确数据源。

---

## Open Questions

- Q1: FragmentOutputStates 是否需要包含 blend state per attachment，还是 Vulkan GPL 将其放在 color blend library 中？（GPL 模式下 fragment output 和 color blend 是分开的 library — 待验证当前 PipelineLibrary 的 4 部分划分）
