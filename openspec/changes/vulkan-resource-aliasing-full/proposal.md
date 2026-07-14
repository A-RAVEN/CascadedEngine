## Why

当前 Vulkan aliasing 实现虽经 `fix-vma-aliased-pool-allocation` 和 `fix-vulkan-aliasing-resource-binding` 修复后可分配 aliased pool 并绑定资源，但距离 D3D12 的 `AliasedMemoryAllocator` 完整实现仍有架构差距：

- D3D12 使用 `VirtualBlock`（512MB）做真正的子分配：per-batch alloc/free 生命周期，空闲区间自动回收
- D3D12 多 pool 自动扩展（`VirtualBlock` 满时创建新 block）
- D3D12 分离不同 `D3D12_HEAP_TYPE`（DEFAULT / UPLOAD）
- Vulkan 当前只有一个裸 VMA allocation + 手动 offset 计算，无生命周期管理、无多 pool、无 buffer/image 分离

## What Changes

- **VmaPool 子分配**: 用 VMA custom pool (`VmaPool`) + `vmaCreateBuffer`/`vmaCreateImage` 替代手动 `vkBind*Memory`，VMA 自动管理子分配和空闲区间
- **Per-batch 生命周期**: 实现 `AllocResource(batchStart)` / `FreeResource(batchEnd)` 接口，非重叠资源自动复用内存
- **多 Pool 自动扩展**: 当 pool 空间不足时自动创建新 pool
- **Buffer/Image 分离 Pool**: 针对 NV GPU 的 separate memory type bits 限制，buffer 和 image 使用不同 pool
- **跨帧 Pool 复用**: 替代每帧分配+释放，pool 跨帧保留，帧末仅 reset 子分配状态

## Capabilities

### Modified Capabilities
- `vulkan-resource-aliasing`: 从"单 pool 手动 bind"升级为"多 pool 子分配 + per-batch 生命周期 + 跨帧复用"

## Impact

- `VulkanResourceAliasing` — 重构为 VmaPool 管理
- `VulkanGraphLocalResourceManager` — AllocateAliasedResources 适配新接口
- D3D12 `AliasedMemoryAllocator` 作为参考架构
