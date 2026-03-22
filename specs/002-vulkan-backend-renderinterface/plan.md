# Implementation Plan: Vulkan Backend RenderInterface Implementation

**Branch**: `002-vulkan-backend-renderinterface` | **Date**: 2026-03-22 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/002-vulkan-backend-renderinterface/spec.md`

## Summary

Implement VulkanRenderBackendNew to fully implement the Interface/RenderInterface contract, enabling Vulkan as a rendering backend option for the CascadedEngine. The implementation will use VulkanMemoryAllocator for memory management, ShaderCompilerSlang for SPIR-V shader compilation, and support VK_EXT_graphics_pipeline_library for optimized pipeline creation. Reference implementations: D3D12RenderBackend (RenderInterface patterns), VulkanRenderBackend (Vulkan API patterns).

## Technical Context

**Language/Version**: C++20 (CMake 3.12+, MSVC toolchain)
**Primary Dependencies**:
- Vulkan SDK 1.2+ (Vulkan-Hpp)
- VulkanMemoryAllocator (VMA) - already integrated
- ShaderCompilerSlang - already integrated
- Interface/RenderInterface - engine abstraction layer
- CACore - logging and utilities

**Storage**: N/A (GPU memory via VMA, no persistent storage)
**Testing**: Existing VulkanRendererBackendTester project (Test/VulkanRendererBackendTester)
**Target Platform**: Windows x64 (Vulkan 1.2 compatible GPU)
**Project Type**: Engine module (dynamic library, MODULE in CMake)
**Performance Goals**:
- Pipeline creation 30%+ faster with graphics pipeline library
- 60+ fps for basic rendering scenarios
- Minimal hitching during shader compilation

**Constraints**:
- Must implement all CRenderBackend pure virtual methods
- Must maintain compatibility with existing RenderInterface contract
- Memory aliasing for temporary resources to reduce memory footprint

**Scale/Scope**:
- Single GPU rendering (no multi-GPU)
- Graphics pipelines only (compute deferred)
- Basic synchronization (advanced features out of scope)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### I. 模块优先的引擎架构 ✅

| 检查项 | 状态 | 说明 |
|--------|------|------|
| 新增能力在最小可负责模块内 | ✅ PASS | VulkanRenderBackendNew 是独立模块 |
| 跨模块依赖在 CMake 中显式声明 | ✅ PASS | 依赖 RenderInterface, ShaderCompiler, CACore 等 |
| 避免全局可变状态隐式耦合 | ✅ PASS | 通过 CRenderBackend 接口访问 |

### II. 以契约驱动的后端一致性 ✅

| 检查项 | 状态 | 说明 |
|--------|------|------|
| 渲染能力先在抽象层定义 | ✅ PASS | 实现 Interface/RenderInterface 契约 |
| 所有受支持后端实现一致 | ⏳ IN PROGRESS | 与 D3D12RenderBackend 对齐 |
| 契约变更提供验证覆盖 | ✅ PASS | 使用 VulkanRendererBackendTester |

### III. 可确定复现的资产与着色器流水线 ✅

| 检查项 | 状态 | 说明 |
|--------|------|------|
| 着色器编译可稳定复现 | ✅ PASS | 使用 ShaderCompilerSlang + eSpirV |
| 着色器接口变更同步更新 | ✅ PASS | 参考 D3D12 ShaderImporter 模式 |

### IV. 测试与验证门禁（不可协商） ✅

| 检查项 | 状态 | 说明 |
|--------|------|------|
| 自动化验证补充或更新 | ✅ PASS | VulkanRendererBackendTester 可用 |
| 测试先失败后通过 | ⏳ PENDING | 实现阶段执行 |

### V. 性能预算与可调试性 ✅

| 检查项 | 状态 | 说明 |
|--------|------|------|
| 声明预期成本影响 | ✅ PASS | SC-004: 30%+ pipeline creation improvement |
| 提供分析钩子/计数器/日志 | ✅ PASS | 使用 CACore 日志系统 |

**Overall Gate Status**: ✅ PASS - 可以进入 Phase 0

## Project Structure

### Documentation (this feature)

```text
specs/002-vulkan-backend-renderinterface/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
VulkanRenderBackendNew/
├── CMakeLists.txt                    # Build configuration
├── private/
│   ├── RenderBackend_Vulkan.h/cpp    # Main backend class (CRenderBackend impl)
│   ├── pch.h                         # Precompiled header
│   │
│   ├── VulkanObjects/                # GPU object implementations
│   │   ├── VulkanBuffer.h/cpp        # GPUBuffer implementation
│   │   ├── VulkanTexture.h/cpp       # GPUTexture implementation
│   │   ├── VulkanShaderStruct.h/cpp  # ShaderStruct implementation
│   │   └── VulkanWindowHandle.h/cpp  # WindowHandle implementation
│   │
│   ├── VulkanObjectManaging/         # Vulkan object managers (existing)
│   │   ├── DescriptorSetLayoutManager.h/cpp
│   │   ├── PipelineLayoutManager.h/cpp
│   │   ├── VertexInputStateManager.h/cpp
│   │   └── FragmentOutputStateManager.h
│   │
│   ├── PipelineStates/               # Pipeline state objects (existing)
│   │   ├── VertexInputStates.h
│   │   ├── FragmentShaderStates.h
│   │   ├── FragmentOutputStates.h
│   │   ├── PipelineLayout.h
│   │   └── ShaderModule.h
│   │
│   ├── PipelineLibrary/              # Graphics Pipeline Library support (NEW)
│   │   ├── VulkanPipelineLibrary.h/cpp
│   │   └── PipelineLibraryCache.h/cpp
│   │
│   ├── GPUGraph/                     # Graph execution (NEW)
│   │   ├── VulkanGraphExecutor.h/cpp       # Main executor (CompileAndExecute)
│   │   ├── VulkanGraphLocalResourceManager.h/cpp  # Graph-local resources with aliasing
│   │   ├── VulkanResourceBindingInstance.h/cpp    # Shader resource bindings
│   │   └── VulkanPassRWState.h/cpp         # Pass read/write state tracking
│   │
│   ├── ResourceManagement/           # Memory management (NEW)
│   │   ├── VulkanMemoryManager.h/cpp
│   │   ├── VulkanCommandListManager.h/cpp  # Command pool & buffer allocation
│   │   └── VulkanResourceAliasing.h/cpp
│   │
│   ├── ShaderLibrary/                # Shader import (existing, extend)
│   │   ├── ShaderLibrary.h/cpp
│   │   └── ShaderImporter_Vulkan.h/cpp (NEW)
│   │
│   ├── VulkanQueue/                  # Queue management (existing)
│   │   └── QueueContext.h/cpp
│   │
│   └── Utils/                        # Utilities (existing)
│       ├── VulkanDebug.h
│       ├── VulkanIncludes.h
│       ├── VulkanSubobjectBase.h/cpp
│       ├── HashContainer.h
│       └── TypeTraits.h
│
Test/VulkanRendererBackendTester/     # Existing test project
├── private/
│   └── Main.cpp
```

**Structure Decision**: Extend existing VulkanRenderBackendNew module structure with new subdirectories for PipelineLibrary, GPUGraph, ResourceManagement, and VulkanObjects. This maintains consistency with D3D12RenderBackend organization while accommodating Vulkan-specific patterns.

## Complexity Tracking

> No constitution violations requiring justification.

## Implementation Phases

### Phase 1: Core Infrastructure (P1 User Stories 1, 2, 5)

1. **RenderBackend_Vulkan Enhancement**
   - Implement all CRenderBackend pure virtual methods
   - Initialize VMA allocator
   - Setup debug messenger

2. **VulkanObjects Implementation**
   - VulkanBuffer: GPUBuffer interface with VMA allocation
   - VulkanTexture: GPUTexture interface with VMA allocation
   - VulkanWindowHandle: Surface + Swapchain management
   - VulkanShaderStruct: Descriptor set layout management

3. **ResourceManagement**
   - VulkanMemoryManager: VMA wrapper
   - Reference counting for long-lived resources
   - Basic memory aliasing support

### Phase 2: Pipeline & Shader (P1/P2 User Stories 3)

1. **PipelineLibrary**
   - VK_EXT_graphics_pipeline_library detection
   - VulkanPipelineLibrary: Separate compilation of pipeline parts
   - PipelineLibraryCache: Hash-based caching

2. **ShaderLibrary Enhancement**
   - ShaderImporter_Vulkan: SPIR-V compilation via ShaderCompilerSlang
   - Metadata extraction for descriptor bindings

### Phase 3: Graph Execution (P2 User Story 4)

1. **GPUGraph Infrastructure** (Reference: D3D12GPUGraphExecutor)
   - VulkanGraphExecutor: Main executor with CompileAndExecute method
   - VulkanGraphLocalResourceManager: Graph-local resource allocation with memory aliasing
   - VulkanResourceBindingInstance: Shader resource binding for descriptor sets
   - VulkanPassRWState: Track resource read/write states per pass

2. **Execution Pipeline**
   - Prepare(): Collect resources, shader bindings, pass RW states
   - BuildDependencyFreeBatchs(): Analyze pass dependencies, create execution batches
   - BuildResourceUsageRanges(): Track resource lifetimes across batches
   - AllocateAliasedResources(): Memory aliasing for non-overlapping lifetimes
   - PrepareBatchResourceBarriers(): Generate vkCmdPipelineBarrier calls
   - BuildPipelineStates(): Create pipeline objects for each batch
   - Execute(): Record command buffers and submit to queues

3. **Cross-Queue Synchronization**
   - Graphics queue for render passes
   - Compute queue for async compute (future)
   - Fence-based synchronization between queues

4. **VulkanCommandList**
   - Command buffer wrapper
   - Render pass management
   - Resource binding helpers
   - Render pass management

## Dependencies on Other Projects

| Project | Dependency Type | Usage |
|---------|-----------------|-------|
| Interface/RenderInterface | Interface | CRenderBackend contract to implement |
| Interface/ShaderCompiler | Interface | Shader compilation interface |
| ShaderCompilerSlang | Implementation | SPIR-V shader compilation |
| CACore | Utility | Logging, memory, strings |
| ExternalLib/VulkanMemoryAllocator | External | GPU memory allocation |
| ExternalLib/VulkanSDK | External | Vulkan API |

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| VK_EXT_graphics_pipeline_library not available | Medium | Medium | Graceful fallback to monolithic pipelines |
| ShaderCompilerSlang SPIR-V bugs | Medium | High | Direct modification allowed per spec |
| VMA integration complexity | Low | Medium | Reference VulkanRenderBackend implementation |
| RenderInterface API changes | Low | High | D3D12RenderBackend as reference implementation |

## Next Steps

1. Run `/speckit.tasks` to generate detailed task breakdown
2. Begin Phase 1 implementation with VulkanObjects
3. Iterate based on VulkanRendererBackendTester validation
