# Research: Vulkan Backend RenderInterface Implementation

**Feature**: 002-vulkan-backend-renderinterface | **Date**: 2026-03-22

## Overview

This document consolidates research findings for implementing VulkanRenderBackendNew to support the Interface/RenderInterface contract.

## Technology Stack Decisions

### Vulkan API Version

**Decision**: Vulkan 1.2

**Rationale**:
- Provides timeline semaphores, descriptor indexing as core features
- Widely supported on modern GPUs (2018+)
- Balance between feature availability and hardware compatibility

**Alternatives Considered**:
- Vulkan 1.0: Too limited, lacks modern features
- Vulkan 1.3: Less widely supported, dynamic rendering could simplify code but not required

### Memory Allocation

**Decision**: VulkanMemoryAllocator (VMA)

**Rationale**:
- Already integrated in project (ExternalLib/VulkanMemoryAllocator)
- Industry standard for Vulkan memory management
- Reference implementation available in VulkanRenderBackend
- Supports memory aliasing for resource optimization

**Alternatives Considered**:
- Custom allocator: Higher development cost, reinventing the wheel
- GPUOpen VMA: Same library, already integrated

**Reference**: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/

### Shader Compilation

**Decision**: ShaderCompilerSlang::IShaderCompilerManager with eSpirV target

**Rationale**:
- Already integrated in project
- Consistent with D3D12RenderBackend approach
- Supports SPIR-V output for Vulkan
- Allows direct bug fixes if needed

**Implementation Pattern**:
- Reference: D3D12RenderBackend/ShaderLibrary/ShaderImporter_D12
- Extract metadata (descriptor bindings, push constants)
- Compile to SPIR-V with reflection data

### Pipeline Library

**Decision**: VK_EXT_graphics_pipeline_library with graceful fallback

**Rationale**:
- Significantly reduces pipeline creation time
- Enables separate compilation of pipeline parts
- Allows caching and reuse of common state
- Spec requires 30%+ performance improvement

**Fallback Strategy**:
- Check extension availability at device creation
- If not available, use monolithic pipeline creation
- Maintain same interface regardless of implementation

**Reference**: https://docs.vulkan.org/samples/latest/samples/extensions/graphics_pipeline_library/README.html

## Implementation Patterns

### Resource Lifecycle Management

**Decision**: Hybrid model (reference counting + graph-scoped + memory aliasing)

**Components**:
1. **Long-lived resources**: Reference counting via shared_ptr
   - Buffers, textures held by application
   - Automatic destruction when no references remain

2. **Graph-scoped resources**: Lifecycle tied to GPUGraph execution
   - Temporary buffers, textures during rendering
   - Freed after graph execution completes

3. **Memory aliasing**: Non-overlapping lifetimes share memory
   - Reduce memory footprint
   - Track resource lifetimes within graph
   - Allocate from same memory pool when safe

### Pipeline Caching

**Decision**: Hash-based cache with state combination keys

**Implementation**:
```cpp
// Key: Hash of render state combination
using PipelineCacheKey = size_t;

// Cache structure
unordered_map<PipelineCacheKey, vk::Pipeline> m_PipelineCache;

// Hash generation
PipelineCacheKey GenerateKey(const RenderStateCombination& state) {
    // Hash vertex input, fragment output, shader modules
    // Similar to D3D12RenderBackend approach
}
```

### Logging Strategy

**Decision**: CACore logging system

**Key Operations to Log**:
- Backend initialization/shutdown
- Resource creation/destruction
- Pipeline compilation (with timing)
- Extension availability
- Error conditions

## Reference Implementations

### D3D12RenderBackend (Primary Reference for Interface)

| Component | Purpose |
|-----------|---------|
| RenderBackend_D3D12 | CRenderBackend implementation pattern |
| D3DBufferObject | GPUBuffer implementation pattern |
| D3DImageObject | GPUTexture implementation pattern |
| ShaderImporter_D12 | Shader import pattern |
| GPUGraphExecutor | Graph execution pattern |
| PipelineStatesObject | PSO creation pattern |

### VulkanRenderBackend (Primary Reference for Vulkan API)

| Component | Purpose |
|-----------|---------|
| RenderBackend_Vulkan | Vulkan instance/device setup |
| QueueContext | Queue management |
| DescriptorSetLayoutManager | Descriptor set patterns |
| VertexInputStateManager | Vertex input state |

## Extension Requirements

### Required Extensions

| Extension | Purpose |
|-----------|---------|
| VK_KHR_swapchain | Window presentation |
| VK_KHR_surface | Surface creation |

### Optional Extensions (with fallback)

| Extension | Purpose | Fallback |
|-----------|---------|----------|
| VK_EXT_graphics_pipeline_library | Pipeline library | Monolithic pipelines |
| VK_KHR_pipeline_library | Pipeline library base | Monolithic pipelines |

## Performance Considerations

### Pipeline Creation Optimization

**Goal**: 30%+ faster than monolithic for shared state

**Strategy**:
1. Cache vertex input interface libraries
2. Cache fragment output interface libraries
3. Only compile unique shader combinations
4. Link pre-compiled libraries for final pipeline

### Memory Efficiency

**Goals**:
- Minimize memory allocations during rendering
- Reuse memory across frames where possible
- Support memory aliasing for temporary resources

### GPUGraph Execution Pattern

**Decision**: Follow D3D12GPUGraphExecutor implementation pattern

**Rationale**:
- Proven architecture in D3D12RenderBackend
- Handles complex dependency management
- Supports memory aliasing for efficiency
- Clear separation of concerns

**Key Components**:
1. **VulkanGraphExecutor**: Main entry point (CompileAndExecute)
2. **VulkanGraphLocalResourceManager**: Graph-local resource allocation
3. **VulkanResourceBindingInstance**: Shader resource bindings
4. **VulkanPassRWState**: Per-pass resource state tracking

**Execution Flow**:
```
1. Prepare() - Collect resources, shader bindings, pass RW states
2. BuildDependencyFreeBatchs() - Analyze dependencies, create batches
3. BuildResourceUsageRanges() - Track resource lifetimes
4. AllocateAliasedResources() - Memory aliasing allocation
5. PrepareBatchResourceBarriers() - Generate barriers
6. BuildPipelineStates() - Create PSOs
7. Execute() - Record and submit command buffers
```

**Reference**: D3D12RenderBackend/private/GPUGraph/GPUGraphExecutor.cpp

## Open Questions (Resolved)

All critical questions resolved during `/speckit.clarify` session:

1. Pipeline cache key strategy → Hash of render state combination
2. Resource lifecycle → Hybrid: refcount + graph-scoped + aliasing
3. MVP scope → Buffer + Texture + Pipeline + Swapchain
4. Vulkan version → 1.2
5. Logging → CACore logging system
6. Memory allocation → VulkanMemoryAllocator (VMA)
7. Shader import → ShaderCompilerSlang::IShaderCompilerManager + eSpirV

## Conclusion

Research phase complete. All technical decisions documented and aligned with constitution requirements. Ready to proceed to Phase 1 design and implementation planning.
