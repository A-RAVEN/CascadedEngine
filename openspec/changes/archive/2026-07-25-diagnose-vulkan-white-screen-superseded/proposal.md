## Why

Vulkan 后端 `TestSimpleTriangle` 测试窗口显示白屏（无三角形），无崩溃、无 validation error。此前已修复 shader 资源路径问题（shader 成功导入，15 files / 28 programs），但三角形仍未渲染。问题出在整个调用链的某个未知环节，需要系统性地逐点诊断，而非猜测式修复。

## What Changes

对 TestSimpleTriangle 完整调用链（初始化 + 每帧执行）中的 **17 个可疑点** 逐点添加诊断日志：

1. **初始化阶段**（8 点）：项目根推导、源目录 fallback、资源根路径、ShaderLibrary 缓存 key 一致性、PathHash 规范化、序列化、Swapchain 创建、Backbuffer ImageHandle
2. **每帧执行阶段**（9 点）：CollectShaderBindings GetShaderFileInfo 返回值、Pipeline 创建结果、RenderPass 兼容性、Vertex Input 匹配、Draw call 执行、Present 结果

每个诊断点输出 `[DIAG-<编号>]` 标记的关键状态，一次性运行 `--headless 5` 收集所有输出，确定故障位置后再针对修复。

## Capabilities

### New Capabilities
- `vulkan-render-diagnostics`: Vulkan 渲染管线的逐点诊断框架，通过带编号的诊断标记快速定位白屏等无声故障

### Modified Capabilities
- _（纯诊断变更，不修改已有 spec 的行为契约）_

## Impact

- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`: Init + GetShaderFileInfo 路径诊断
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`: BuildPipelineStates + RecordRenderPass + PresentWindows 诊断
- `VulkanRenderBackendNew/private/VulkanObjects/VulkanWindowHandle.cpp`: Swapchain/Present 诊断
- `VulkanRenderBackendNew/private/ShaderLibrary/ShaderImporter_Vulkan.cpp`: PathHash 一致性诊断
