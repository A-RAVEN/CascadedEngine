# Feature Specification: Vulkan Backend RenderInterface Implementation

**Feature Branch**: `002-vulkan-backend-renderinterface`
**Created**: 2026-03-22
**Status**: Draft
**Input**: User description: "VulkanRenderBackendNew是一个仅有少量实现的空壳，我需要将其完善，使其基本实现Interface/RenderInterface的功能，目前另一个初步实现了RenderInterface的子项目是D3D12RenderBackend，可以加以参考。在vulkan api相关的实现细节上，可以参考VulkanRenderBackend的实现（它目前不再适配RenderInterface，应该也不能编译成功），此外，在实现PipelineObject创建和缓存机制时，我希望可以支持vulkan的Graphics pipeline library，参考文档https://docs.vulkan.org/samples/latest/samples/extensions/graphics_pipeline_library/README.html"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Core RenderBackend Interface Implementation (Priority: P1)

As a graphics developer, I want VulkanRenderBackendNew to implement the CRenderBackend interface so that the Vulkan backend can be used as a rendering backend in the engine.

**Why this priority**: Without implementing the core interface, the backend cannot be integrated with the rendering system at all.

**Independent Test**: Can be fully tested by creating a VulkanRenderBackendNew instance and calling all CRenderBackend virtual methods to verify they return valid (non-null) results.

**Acceptance Scenarios**:

1. **Given** a VulkanRenderBackendNew instance, **When** CreateGPUBuffer is called with valid parameters, **Then** a valid GPUBuffer object is returned
2. **Given** a VulkanRenderBackendNew instance, **When** CreateGPUTexture is called with valid parameters, **Then** a valid GPUTexture object is returned
3. **Given** a VulkanRenderBackendNew instance, **When** CreateShaderStruct is called, **Then** a valid ShaderStruct object is returned
4. **Given** a VulkanRenderBackendNew instance, **When** GetWindowHandle is called with a valid window, **Then** a valid WindowHandle is returned
5. **Given** a VulkanRenderBackendNew instance, **When** AnyWindowRunning is called, **Then** it returns the correct window state

---

### User Story 2 - GPU Resource Management (Priority: P1)

As a graphics developer, I want the Vulkan backend to properly manage GPU resources (buffers, textures) so that rendering operations have the necessary memory resources.

**Why this priority**: GPU resources are fundamental to any rendering operation.

**Independent Test**: Can be fully tested by creating various buffer and texture types, writing data to them, and verifying the data can be read back correctly.

**Acceptance Scenarios**:

1. **Given** a buffer creation request, **When** the buffer is created with specific size and usage flags, **Then** the buffer has the correct memory allocation
2. **Given** a texture creation request, **When** the texture is created with specific dimensions and format, **Then** the texture has the correct image allocation
3. **Given** allocated resources, **When** the backend is released, **Then** all resources are properly freed

---

### User Story 3 - Pipeline State Object with Graphics Pipeline Library (Priority: P2)

As a graphics developer, I want the Vulkan backend to support pipeline state creation using VK_EXT_graphics_pipeline_library so that pipeline creation is faster and more efficient through caching and reuse.

**Why this priority**: Pipeline creation performance is critical for runtime shader compilation and reducing hitching. This is a key differentiator from the D3D12 backend.

**Independent Test**: Can be fully tested by creating multiple pipelines that share common state (vertex input, output format) and verifying that shared libraries are reused.

**Acceptance Scenarios**:

1. **Given** a pipeline creation request, **When** the pipeline is created, **Then** it uses graphics pipeline library for separate compilation of pipeline parts
2. **Given** multiple pipelines with shared vertex input state, **When** pipelines are created, **Then** the vertex input interface library is reused
3. **Given** multiple pipelines with shared fragment output state, **When** pipelines are created, **Then** the fragment output interface library is reused
4. **Given** pre-compiled pipeline libraries, **When** linking a full pipeline, **Then** the linking operation completes faster than monolithic compilation

---

### User Story 4 - GPUGraph Execution (Priority: P2)

As a graphics developer, I want VulkanRenderBackendNew to execute GPUGraph commands so that rendering workloads can be submitted to the GPU.

**Why this priority**: Without graph execution, the backend cannot perform actual rendering work.

**Independent Test**: Can be fully tested by creating a simple GPUGraph with draw commands and verifying the output is rendered correctly.

**Acceptance Scenarios**:

1. **Given** a valid GPUGraph, **When** ExecuteGraph is called, **Then** the graph commands are translated to GPU commands
2. **Given** a GPUGraph with multiple render passes, **When** ExecuteGraph is called, **Then** all render passes execute in the correct order
3. **Given** a GPUGraph with compute operations, **When** ExecuteGraph is called, **Then** compute shaders execute correctly

---

### User Story 5 - Window and Swapchain Management (Priority: P1)

As a graphics developer, I want the Vulkan backend to manage windows and swapchains so that rendered content can be displayed on screen.

**Why this priority**: Without window management, the backend cannot produce visible output.

**Independent Test**: Can be fully tested by creating a window, acquiring a swapchain image, and presenting it.

**Acceptance Scenarios**:

1. **Given** a window creation request, **When** GetWindowHandle is called, **Then** a valid surface and swapchain are created
2. **Given** a window with a swapchain, **When** a frame is rendered, **Then** the image can be acquired and presented
3. **Given** a window resize event, **When** the swapchain is out of date, **Then** the swapchain is recreated correctly

---

### Edge Cases

- What happens when GPU device memory is exhausted? → Should gracefully fail and log error
- What happens when a required extension is not available? → Should fail initialization with clear error message
- What happens when the graphics pipeline library extension is not supported? → Should fall back to monolithic pipeline creation
- What happens when a shader compilation fails? → Should return null/invalid shader object with error logged
- What happens when swapchain recreation fails? → Should retry with different parameters or report fatal error

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: VulkanRenderBackendNew MUST implement all pure virtual methods of CRenderBackend interface
- **FR-002**: The backend MUST create and manage buffers that implement the GPUBuffer interface
- **FR-003**: The backend MUST create and manage images that implement the GPUTexture interface
- **FR-004**: The backend MUST create and manage shader structs that implement the ShaderStruct interface
- **FR-005**: The backend MUST create and manage window handles with surfaces and swapchains
- **FR-006**: The backend MUST execute GPUGraph commands by translating them to GPU commands
- **FR-007**: The backend MUST properly manage GPU object lifetimes using reference counting for long-lived resources and graph-scoped lifecycle for temporary resources with memory aliasing support
- **FR-008**: Pipeline creation MUST support graphics pipeline library extension when available
- **FR-009**: Pipeline libraries MUST be cached using hash of render state combination as key and reused for pipelines with shared state
- **FR-010**: The backend MUST support required extensions for swapchain and pipeline library
- **FR-011**: The backend MUST fall back gracefully when optional extensions are not available
- **FR-012**: The backend MUST use CACore logging system to record key operations for debugging
- **FR-013**: Shader import MUST use ShaderCompilerSlang::IShaderCompilerManager with eSpirV target type, referencing D3D12RenderBackend/ShaderLibrary/ShaderImporter_D12 implementation pattern
- **FR-014**: GPUGraph execution MUST implement VulkanGraphExecutor following D3D12GPUGraphExecutor pattern:
  - Resource collection and state tracking (PassRWState)
  - Dependency analysis and batch construction
  - Resource lifetime management with memory aliasing
  - Pipeline barrier generation (vkCmdPipelineBarrier)
  - Cross-queue synchronization (graphics/compute queues)
  - Command buffer recording and submission

### Key Entities

- **VulkanRenderBackend**: Main backend class implementing CRenderBackend, manages instance, device, and queues
- **VulkanGPUBuffer**: Implements GPUBuffer interface, wraps buffer and VMA allocation
- **VulkanGPUTexture**: Implements GPUTexture interface, wraps image, image view, and VMA allocation
- **VulkanShaderStruct**: Implements ShaderStruct interface, manages descriptor set layouts
- **VulkanWindowHandle**: Implements WindowHandle interface, manages surface and swapchain
- **VulkanPipelineLibrary**: Manages cached pipeline library parts (vertex input, pre-rasterization, fragment, fragment output)
- **VulkanMemoryAllocator (VMA)**: External library for GPU memory management, used via VulkanMemoryAllocator integration
- **VulkanShaderImporter**: Handles shader compilation and metadata extraction using ShaderCompilerSlang::IShaderCompilerManager with eSpirV target
- **VulkanGraphExecutor**: Executes GPUGraph following D3D12GPUGraphExecutor pattern
- **VulkanGraphLocalResourceManager**: Manages graph-local resources with memory aliasing support
- **VulkanResourceBindingInstance**: Shader resource binding implementation for descriptor sets
- **VulkanCommandListManager**: Manages command pools and allocates command buffers (returns vk::CommandBuffer&, no wrapper class needed)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All CRenderBackend interface methods are implemented and return valid results for valid inputs
- **SC-002**: GPU resources (buffers, textures) can be created, written to, and read from correctly
- **SC-003**: A simple triangle can be rendered to a window using the Vulkan backend
- **SC-004**: Pipeline creation using graphics pipeline library is at least 30% faster than monolithic creation for pipelines with shared state
- **SC-005**: Multiple windows can be created and rendered to simultaneously
- **SC-006**: The backend compiles without errors when integrated into the build system

## Clarifications

### Session 2026-03-23

- Q: GPUGraph执行如何实现？ → A: 参考D3D12RenderBackend/GPUGraph/GPUGraphExecutor，实现VulkanGraphExecutor，包含：资源收集、依赖分析、批次构建、生命周期管理、内存别名、Barrier生成、跨队列同步、命令录制提交

### Session 2026-03-22

- Q: Pipeline library 缓存应该如何标识和查找？ → A: 使用渲染状态组合的哈希值作为键（与D3D12RenderBackend类似实现）
- Q: GPU资源（Buffer、Texture）的生命周期应该如何管理？ → A: 混合模式：长期资源用引用计数；GPUGraph临时资源生命周期与Graph执行周期关联；使用内存aliasing复用非重叠生命周期的资源内存
- Q: 实现范围的优先级？第一阶段应该实现哪些功能？ → A: 先实现核心渲染路径：Buffer+Texture+Pipeline+Swapchain（MVP）
- Q: 目标 Vulkan API 版本要求？ → A: Vulkan 1.2（平衡特性与兼容性）
- Q: 日志和调试信息的记录策略？ → A: 使用CACore日志系统记录关键操作
- Q: 内存分配使用什么库？ → A: 使用VulkanMemoryAllocator（VMA），参考VulkanRenderBackend和官方文档
- Q: Shader导入如何实现？ → A: 参考D3D12RenderBackend/ShaderLibrary/ShaderImporter_D12，使用ShaderCompilerSlang::IShaderCompilerManager编译shader并提取元数据，shaderTargetType使用eSpirV，可直接修改ShaderCompilerSlang修复bug

### Session 2026-03-23

- Q: GPUGraph执行实现参考？ → A: 参考D3D12GPUGraphExecutor实现模式：资源状态收集、依赖分析构建批次、资源生命周期管理+内存aliasing、Pipeline屏障生成、跨队列同步、命令缓冲录制提交
- Q: GPU资源（Buffer、Texture）的生命周期应该如何管理？ → A: 混合模式：长期资源用引用计数；GPUGraph临时资源生命周期与Graph执行周期关联；使用内存aliasing复用非重叠生命周期的资源内存
- Q: 实现范围的优先级？第一阶段应该实现哪些功能？ → A: 先实现核心渲染路径：Buffer+Texture+Pipeline+Swapchain（MVP）
- Q: 目标 Vulkan API 版本要求？ → A: Vulkan 1.2（平衡特性与兼容性）
- Q: 日志和调试信息的记录策略？ → A: 使用CACore日志系统记录关键操作
- Q: 内存分配使用什么库？ → A: 使用VulkanMemoryAllocator（VMA），参考VulkanRenderBackend和官方文档
- Q: Shader导入如何实现？ → A: 参考D3D12RenderBackend/ShaderLibrary/ShaderImporter_D12，使用ShaderCompilerSlang::IShaderCompilerManager编译shader并提取元数据，shaderTargetType使用eSpirV，可直接修改ShaderCompilerSlang修复bug

## Assumptions

- Vulkan SDK is available and properly configured in the build environment
- VulkanMemoryAllocator (VMA) library is available and integrated (reference: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/)
- ShaderCompilerSlang::IShaderCompilerManager is available for shader compilation with SPIR-V output support
- Target Vulkan API version is 1.2 (balance of features and compatibility)
- Graphics pipeline library extension is available on target hardware (with graceful fallback if not)
- The existing VulkanRenderBackend code provides valid reference for API usage patterns
- The D3D12RenderBackend provides valid reference for RenderInterface implementation patterns
- Debug validation layers are available for development testing

## Out of Scope (Phase 1)

The following features are explicitly out of scope for the initial MVP implementation:

- Compute pipeline support (to be added in future iteration)
- Advanced synchronization features (timeline semaphores beyond basic usage)
- Multi-GPU support
- Ray tracing support
- Mesh shaders
- Advanced memory management features beyond basic aliasing
