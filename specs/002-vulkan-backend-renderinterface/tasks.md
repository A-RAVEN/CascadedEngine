# Tasks: Vulkan Backend RenderInterface Implementation

**Input**: Design documents from `/specs/002-vulkan-backend-renderinterface/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, quickstart.md

**Tests**: Tests are not explicitly requested. Validation via VulkanRendererBackendTester.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- **Project root**: `VulkanRenderBackendNew/`
- **Private headers/source**: `VulkanRenderBackendNew/private/`
- **Test project**: `Test/VulkanRendererBackendTester/`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization, build configuration, and core utilities

- [X] T001 Update CMakeLists.txt for VulkanRenderBackendNew with all required dependencies (Vulkan SDK, VMA, RenderInterface, CACore)
- [X] T002 [P] Create precompiled header VulkanRenderBackendNew/private/pch.h with common Vulkan and engine includes
- [X] T003 [P] Create VulkanRenderBackendNew/private/Utils/VulkanIncludes.h with Vulkan-Hpp and VMA includes
- [X] T004 [P] Create VulkanRenderBackendNew/private/Utils/VulkanDebug.h with debug messenger and validation utilities
- [X] T005 [P] Create VulkanRenderBackendNew/private/Utils/HashContainer.h with hash utilities for cache keys
- [X] T006 [P] Create VulkanRenderBackendNew/private/Utils/TypeTraits.h with Vulkan type conversion traits

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T007 Create VulkanRenderBackendNew/private/Utils/VulkanSubobjectBase.h/cpp with base class for all Vulkan objects
- [X] T008 Create VulkanRenderBackendNew/private/VulkanQueue/QueueContext.h/cpp for queue management (graphics, compute, transfer)
- [X] T009 Create VulkanRenderBackendNew/private/VulkanObjectManaging/DescriptorSetLayoutManager.h/cpp for descriptor set layout caching
- [X] T010 [P] Create VulkanRenderBackendNew/private/PipelineStates/VertexInputStates.h for vertex input state definitions
- [X] T011 [P] Create VulkanRenderBackendNew/private/PipelineStates/FragmentOutputStates.h for fragment output state definitions
- [X] T012 [P] Create VulkanRenderBackendNew/private/PipelineStates/PipelineLayout.h for pipeline layout definitions
- [X] T013 [P] Create VulkanRenderBackendNew/private/PipelineStates/ShaderModule.h for shader module wrapper
- [X] T014 Create VulkanRenderBackendNew/private/ResourceManagement/VulkanMemoryManager.h/cpp with VMA allocator wrapper
- [X] T015 Create VulkanRenderBackendNew/private/ResourceManagement/VulkanCommandListManager.h/cpp for command pool/buffer management

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Core RenderBackend Interface Implementation (Priority: P1) 🎯 MVP

**Goal**: Implement all CRenderBackend pure virtual methods so VulkanRenderBackendNew can be used as a rendering backend

**Independent Test**: Create VulkanRenderBackendNew instance and call all CRenderBackend virtual methods to verify they return valid results

### Implementation for User Story 1

- [X] T016 [US1] Create VulkanRenderBackendNew/private/RenderBackend_Vulkan.h with VulkanRenderBackend class declaration implementing CRenderBackend
- [X] T017 [US1] Implement RenderBackend_Vulkan.cpp initialization: CreateInstance, PickPhysicalDevice, CreateLogicalDevice, InitVMA
- [X] T018 [US1] Implement RenderBackend_Vulkan.cpp debug messenger setup for validation layers
- [X] T019 [US1] Implement RenderBackend_Vulkan.cpp extension detection for VK_EXT_graphics_pipeline_library
- [X] T020 [US1] Implement RenderBackend_Vulkan.cpp CreateGPUBuffer stub (returns VulkanGPUBuffer)
- [X] T021 [US1] Implement RenderBackend_Vulkan.cpp CreateGPUTexture stub (returns VulkanGPUTexture)
- [X] T022 [US1] Implement RenderBackend_Vulkan.cpp CreateShaderStruct stub (returns VulkanShaderStruct)
- [X] T023 [US1] Implement RenderBackend_Vulkan.cpp GetWindowHandle stub (returns VulkanWindowHandle)
- [X] T024 [US1] Implement RenderBackend_Vulkan.cpp AnyWindowRunning method
- [X] T025 [US1] Add logging for backend initialization and key operations using CACore

**Checkpoint**: User Story 1 complete - CRenderBackend interface implemented (stubs return valid objects)

---

## Phase 4: User Story 2 - GPU Resource Management (Priority: P1)

**Goal**: Properly manage GPU resources (buffers, textures) with VMA allocation and lifecycle management

**Independent Test**: Create various buffer and texture types, write data to them, verify data can be read back correctly

### Implementation for User Story 2

- [X] T026 [P] [US2] Create VulkanRenderBackendNew/private/VulkanObjects/VulkanBuffer.h/cpp implementing GPUBuffer interface with VMA allocation
- [X] T027 [P] [US2] Create VulkanRenderBackendNew/private/VulkanObjects/VulkanTexture.h/cpp implementing GPUTexture interface with VMA allocation
- [X] T028 [US2] Implement VulkanBuffer: Map/Unmap for CPU-accessible buffers with persistent mapping support
- [ ] T029 [US2] Implement VulkanBuffer: Upload data via staging buffer for GPU-only buffers
- [X] T030 [US2] Implement VulkanTexture: Create image, allocate memory, create image view
- [X] T031 [US2] Implement VulkanTexture: Transition image layouts using vkCmdPipelineBarrier
- [ ] T032 [US2] Implement VulkanTexture: Upload texture data via staging buffer
- [X] T033 [US2] Connect VulkanBuffer/VulkanTexture to RenderBackend_Vulkan CreateGPUBuffer/CreateGPUTexture
- [X] T034 [US2] Implement reference counting for long-lived resources (shared_ptr integration)
- [X] T035 [US2] Add logging for resource creation/destruction operations

**Checkpoint**: User Story 2 complete - GPU resources can be created, written, and read

---

## Phase 5: User Story 5 - Window and Swapchain Management (Priority: P1)

**Goal**: Manage windows and swapchains so rendered content can be displayed on screen

**Independent Test**: Create a window, acquire a swapchain image, and present it

### Implementation for User Story 5

- [X] T036 [US5] Create VulkanRenderBackendNew/private/VulkanObjects/VulkanWindowHandle.h/cpp implementing WindowHandle interface
- [X] T037 [US5] Implement VulkanWindowHandle: Create Vulkan surface from platform window
- [X] T038 [US5] Implement VulkanWindowHandle: Create swapchain with proper format, present mode, extent selection
- [X] T039 [US5] Implement VulkanWindowHandle: Create swapchain image views
- [X] T040 [US5] Implement VulkanWindowHandle: AcquireNextImage with semaphore synchronization
- [X] T041 [US5] Implement VulkanWindowHandle: Present with queue present operation
- [X] T042 [US5] Implement VulkanWindowHandle: RecreateSwapchain for resize/minimize events
- [X] T043 [US5] Connect VulkanWindowHandle to RenderBackend_Vulkan GetWindowHandle
- [X] T044 [US5] Update RenderBackend_Vulkan AnyWindowRunning to check window handles

**Checkpoint**: User Story 5 complete - Windows can be created and presented to

---

## Phase 6: User Story 3 - Pipeline State Object with Graphics Pipeline Library (Priority: P2)

**Goal**: Support pipeline state creation using VK_EXT_graphics_pipeline_library for faster pipeline creation through caching

**Independent Test**: Create multiple pipelines that share common state and verify shared libraries are reused

### Implementation for User Story 3

- [X] T045 [P] [US3] Create VulkanRenderBackendNew/private/VulkanObjectManaging/VertexInputStateManager.h/cpp for vertex input state caching
- [X] T046 [P] [US3] Create VulkanRenderBackendNew/private/VulkanObjectManaging/FragmentOutputStateManager.h for fragment output state caching
- [X] T047 [P] [US3] Create VulkanRenderBackendNew/private/VulkanObjectManaging/PipelineLayoutManager.h/cpp for pipeline layout caching
- [X] T048 [US3] Create VulkanRenderBackendNew/private/VulkanObjects/VulkanShaderStruct.h/cpp implementing ShaderStruct interface
- [ ] T049 [US3] Create VulkanRenderBackendNew/private/ShaderLibrary/ShaderImporter_Vulkan.h/cpp using ShaderCompilerSlang::IShaderCompilerManager with eSpirV target
- [ ] T050 [US3] Implement ShaderImporter_Vulkan: Extract descriptor bindings and push constants from compiled SPIR-V
- [ ] T051 [US3] Create VulkanRenderBackendNew/private/PipelineLibrary/VulkanPipelineLibrary.h/cpp for graphics pipeline library management
- [ ] T052 [US3] Implement VulkanPipelineLibrary: Create vertex input interface library
- [ ] T053 [US3] Implement VulkanPipelineLibrary: Create pre-rasterization shaders library
- [ ] T054 [US3] Implement VulkanPipelineLibrary: Create fragment shader library
- [ ] T055 [US3] Implement VulkanPipelineLibrary: Create fragment output interface library
- [ ] T056 [US3] Implement VulkanPipelineLibrary: Link full pipeline from libraries
- [ ] T057 [US3] Create VulkanRenderBackendNew/private/PipelineLibrary/PipelineLibraryCache.h/cpp for hash-based library caching
- [ ] T058 [US3] Implement PipelineLibraryCache: Generate hash key from render state combination
- [ ] T059 [US3] Implement graceful fallback to monolithic pipeline creation when VK_EXT_graphics_pipeline_library unavailable
- [ ] T060 [US3] Connect VulkanShaderStruct and pipeline creation to RenderBackend_Vulkan
- [ ] T061 [US3] Add performance logging for pipeline creation timing

**Checkpoint**: User Story 3 complete - Pipelines can be created with library optimization

---

## Phase 7: User Story 4 - GPUGraph Execution (Priority: P2)

**Goal**: Execute GPUGraph commands by translating them to Vulkan GPU commands

**Independent Test**: Create a simple GPUGraph with draw commands and verify output is rendered correctly

### Implementation for User Story 4

- [ ] T062 [US4] Create VulkanRenderBackendNew/private/GPUGraph/VulkanPassRWState.h/cpp for per-pass resource read/write state tracking
- [ ] T063 [US4] Create VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.h/cpp for shader resource bindings to descriptor sets
- [ ] T064 [US4] Create VulkanRenderBackendNew/private/ResourceManagement/VulkanResourceAliasing.h/cpp for memory aliasing of non-overlapping lifetimes
- [ ] T065 [US4] Create VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.h/cpp for graph-local resource allocation with aliasing
- [ ] T066 [US4] Implement VulkanGraphLocalResourceManager: Allocate temporary buffers and textures
- [ ] T067 [US4] Implement VulkanGraphLocalResourceManager: Track resource lifetimes within graph execution
- [ ] T068 [US4] Create VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h/cpp following D3D12GPUGraphExecutor pattern
- [ ] T069 [US4] Implement VulkanGraphExecutor: Prepare() - Collect resources, shader bindings, pass RW states
- [ ] T070 [US4] Implement VulkanGraphExecutor: BuildDependencyFreeBatchs() - Analyze pass dependencies, create execution batches
- [ ] T071 [US4] Implement VulkanGraphExecutor: BuildResourceUsageRanges() - Track resource lifetimes across batches
- [ ] T072 [US4] Implement VulkanGraphExecutor: AllocateAliasedResources() - Memory aliasing for non-overlapping lifetimes
- [ ] T073 [US4] Implement VulkanGraphExecutor: PrepareBatchResourceBarriers() - Generate vkCmdPipelineBarrier calls
- [ ] T074 [US4] Implement VulkanGraphExecutor: BuildPipelineStates() - Create pipeline objects for each batch
- [ ] T075 [US4] Implement VulkanGraphExecutor: Execute() - Record command buffers and submit to queues
- [ ] T076 [US4] Implement cross-queue synchronization using fences (graphics queue)
- [ ] T077 [US4] Connect VulkanGraphExecutor to RenderBackend_Vulkan ExecuteGraph method
- [ ] T078 [US4] Add logging for graph execution phases and timing

**Checkpoint**: User Story 4 complete - GPUGraph can be executed for rendering

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T079 [P] Update VulkanRendererBackendTester to validate all user stories
- [ ] T080 [P] Add comprehensive error handling and validation for all Vulkan operations
- [ ] T081 [P] Implement proper cleanup and resource destruction in RenderBackend_Vulkan Release()
- [ ] T082 Run quickstart.md validation scenarios
- [ ] T083 Verify 30%+ pipeline creation speedup with graphics pipeline library
- [ ] T084 Verify 60+ fps performance with VulkanRendererBackendTester

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - US1 (Core Interface) can start after Foundational
  - US2 (Resources) can start after Foundational (parallel with US1 for stubs, needs US1 for integration)
  - US5 (Window) can start after Foundational (parallel with US1/US2)
  - US3 (Pipeline) can start after Foundational, benefits from US2 completion
  - US4 (Graph) depends on US2 and US3 completion (needs resources and pipelines)
- **Polish (Phase 8)**: Depends on all user stories being complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational - Connects to US1 Create methods
- **User Story 5 (P1)**: Can start after Foundational - Connects to US1 GetWindowHandle
- **User Story 3 (P2)**: Can start after Foundational - Uses US2 resources for descriptor sets
- **User Story 4 (P2)**: Depends on US2 (resources) and US3 (pipelines) - Integrates all components

### Within Each User Story

- Models/Objects before integration
- Core implementation before connection to RenderBackend
- Logging after core functionality works

### Parallel Opportunities

- All Setup tasks marked [P] can run in parallel
- All Foundational tasks marked [P] can run in parallel
- US1, US2, US5 can run in parallel (with proper integration points)
- US2 and US3 models can run in parallel within their phases

---

## Parallel Example: Foundational Phase

```bash
# Launch all parallel foundational tasks together:
Task: "Create VulkanRenderBackendNew/private/PipelineStates/VertexInputStates.h"
Task: "Create VulkanRenderBackendNew/private/PipelineStates/FragmentOutputStates.h"
Task: "Create VulkanRenderBackendNew/private/PipelineStates/PipelineLayout.h"
Task: "Create VulkanRenderBackendNew/private/PipelineStates/ShaderModule.h"
```

## Parallel Example: User Story 2

```bash
# Launch buffer and texture implementation in parallel:
Task: "Create VulkanRenderBackendNew/private/VulkanObjects/VulkanBuffer.h/cpp"
Task: "Create VulkanRenderBackendNew/private/VulkanObjects/VulkanTexture.h/cpp"
```

---

## Implementation Strategy

### MVP First (User Stories 1, 2, 5 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Core Interface)
4. Complete Phase 4: User Story 2 (Resources)
5. Complete Phase 5: User Story 5 (Window/Swapchain)
6. **STOP and VALIDATE**: Test triangle rendering with VulkanRendererBackendTester
7. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Interface methods callable
3. Add User Story 2 → GPU resources work
4. Add User Story 5 → Can display to screen (MVP complete - can render triangle!)
5. Add User Story 3 → Pipeline optimization
6. Add User Story 4 → Full GPUGraph execution
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (Core Interface)
   - Developer B: User Story 2 (Resources)
   - Developer C: User Story 5 (Window/Swapchain)
3. After US1/US2/US5 complete:
   - Developer A: User Story 3 (Pipeline)
   - Developer B: User Story 4 (Graph)
4. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Reference D3D12RenderBackend for RenderInterface patterns
- Reference VulkanRenderBackend for Vulkan API patterns
- Use VulkanMemoryAllocator (VMA) for all GPU memory
- Use ShaderCompilerSlang with eSpirV target for shader compilation
- Commit after each task or logical group
- Stop at any checkpoint to validate story independently
