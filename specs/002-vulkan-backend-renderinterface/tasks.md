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
- [X] T029 [US2] Implement VulkanBuffer: Upload data via staging buffer for GPU-only buffers (implemented in VulkanGraphExecutor::RecordTransferPass)
- [X] T030 [US2] Implement VulkanTexture: Create image, allocate memory, create image view
- [X] T031 [US2] Implement VulkanTexture: Transition image layouts using vkCmdPipelineBarrier
- [X] T032 [US2] Implement VulkanTexture: Upload texture data via staging buffer (implemented in VulkanGraphExecutor::RecordTransferPass)
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
- [X] T049 [US3] Create VulkanRenderBackendNew/private/ShaderLibrary/ShaderImporter_Vulkan.h/cpp using ShaderCompilerSlang::IShaderCompilerManager with eSpirV target
- [X] T050 [US3] Implement ShaderImporter_Vulkan: Extract descriptor bindings and push constants from compiled SPIR-V
- [X] T051 [US3] Create VulkanRenderBackendNew/private/PipelineLibrary/VulkanPipelineLibrary.h/cpp for graphics pipeline library management
- [X] T052 [US3] Implement VulkanPipelineLibrary: Create vertex input interface library
- [X] T053 [US3] Implement VulkanPipelineLibrary: Create pre-rasterization shaders library
- [X] T054 [US3] Implement VulkanPipelineLibrary: Create fragment shader library
- [X] T055 [US3] Implement VulkanPipelineLibrary: Create fragment output interface library
- [X] T056 [US3] Implement VulkanPipelineLibrary: Link full pipeline from libraries
- [X] T057 [US3] Create VulkanRenderBackendNew/private/PipelineLibrary/PipelineLibraryCache.h/cpp for hash-based library caching
- [X] T058 [US3] Implement PipelineLibraryCache: Generate hash key from render state combination
- [X] T059 [US3] Implement graceful fallback to monolithic pipeline creation when VK_EXT_graphics_pipeline_library unavailable
- [X] T060 [US3] Connect VulkanShaderStruct and pipeline creation to RenderBackend_Vulkan
- [X] T061 [US3] Add performance logging for pipeline creation timing

**Checkpoint**: User Story 3 complete - Pipelines can be created with library optimization

---

## Phase 7: User Story 4 - GPUGraph Execution (Priority: P2)

**Goal**: Execute GPUGraph commands by translating them to Vulkan GPU commands

**Independent Test**: Create a simple GPUGraph with draw commands and verify output is rendered correctly

### Implementation for User Story 4

- [X] T062 [US4] Create VulkanRenderBackendNew/private/GPUGraph/VulkanPassRWState.h/cpp for per-pass resource read/write state tracking
- [X] T063 [US4] Create VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.h/cpp for shader resource bindings to descriptor sets
- [X] T064 [US4] Create VulkanRenderBackendNew/private/ResourceManagement/VulkanResourceAliasing.h/cpp for memory aliasing of non-overlapping lifetimes
- [X] T065 [US4] Create VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.h/cpp for graph-local resource allocation with aliasing
- [X] T066 [US4] Implement VulkanGraphLocalResourceManager: Allocate temporary buffers and textures
- [X] T067 [US4] Implement VulkanGraphLocalResourceManager: Track resource lifetimes within graph execution
- [X] T068 [US4] Create VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h/cpp following D3D12GPUGraphExecutor pattern
- [X] T069 [US4] Implement VulkanGraphExecutor: Prepare() - Collect resources, shader bindings, pass RW states
- [X] T070 [US4] Implement VulkanGraphExecutor: BuildDependencyFreeBatchs() - Analyze pass dependencies, create execution batches
- [X] T071 [US4] Implement VulkanGraphExecutor: BuildResourceUsageRanges() - Track resource lifetimes across batches
- [X] T072 [US4] Implement VulkanGraphExecutor: AllocateAliasedResources() - Memory aliasing for non-overlapping lifetimes
- [X] T073 [US4] Implement VulkanGraphExecutor: PrepareBatchResourceBarriers() - Generate vkCmdPipelineBarrier calls
- [X] T074 [US4] Implement VulkanGraphExecutor: BuildPipelineStates() - Create pipeline objects for each batch
- [X] T075 [US4] Implement VulkanGraphExecutor: Execute() - Record command buffers and submit to queues
- [X] T076 [US4] Implement cross-queue synchronization using fences (graphics queue)
- [X] T077 [US4] Connect VulkanGraphExecutor to RenderBackend_Vulkan ExecuteGraph method
- [X] T078 [US4] Add logging for graph execution phases and timing

### Remaining Work for User Story 4 (Shader Integration)

- [X] T085 [US4] Implement shader module creation from ShaderInfo in BuildPipelineStates()
- [X] T086 [US4] Implement pipeline layout creation from shader reflection data
- [X] T087 [US4] Implement descriptor set population from VulkanShaderStruct in CollectShaderBindings()
- [X] T088 [US4] Implement framebuffer/renderpass caching (currently creates/destroys per pass)
- [X] T089 [US4] Implement staging buffer cleanup via frame-based resource pool
- [X] T090 [US4] Implement async compute queue support (computeCommandBuffer allocation and submission)

**Checkpoint**: User Story 4 complete - GPUGraph can be executed for rendering

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T079 [P] Update VulkanRendererBackendTester to validate all user stories
- [X] T080 [P] Add comprehensive error handling and validation for all Vulkan operations
- [X] T081 [P] Implement proper cleanup and resource destruction in RenderBackend_Vulkan Release()
- [ ] T082 Run quickstart.md validation scenarios
- [ ] T083 Verify 30%+ pipeline creation speedup with graphics pipeline library
- [ ] T084 Verify 60+ fps performance with VulkanRendererBackendTester

### ShaderImporter FR-013 Compliance Fix (Clarified 2026-03-25)

- [X] T091 [US3] Refactor ShaderImporter_Vulkan to use IShaderCompilerManager for shader source compilation (AquireShaderCompilerShared→BeginCompileTask→SetTarget(eSpirV)→Compile→GetResults)
- [X] T092 [US3] Replace manual SPIR-V parsing with ShaderCompilerSlang reflection data extraction from GetResults()→result.m_ReflectionData
- [X] T093 [US3] Implement descriptor binding construction from ShaderReflectionData.m_BindingInfo hierarchy following ConstructShaderDescriptorInfo() pattern
- [X] T094 [US3] Add SetCompiler() method to ShaderImporter_Vulkan to receive IShaderCompilerManager instance (following D3D12ShaderResourceImporter pattern)

---

## Progress Summary

**Total Tasks**: 94
**Completed**: 90
**Remaining**: 4

### Completed User Stories
- ✅ US1: Core RenderBackend Interface (100%)
- ✅ US2: GPU Resource Management (100%)
- ✅ US3: Pipeline State Object with Graphics Pipeline Library (FR-013 compliance complete)
- ✅ US4: GPUGraph Execution - Core (100%)
- ✅ US4: GPUGraph Execution - Shader Integration (100%)
- ✅ US5: Window and Swapchain Management (100%)

### Remaining Work
- **Polish**: 4 tasks (T079, T082-T084)
  - T079: Tester validation
  - T082: Quickstart scenarios
  - T083: Performance verification
  - T084: Performance verification

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

### Current Status (Updated 2026-03-25)

✅ **Feature Complete** - All user stories implemented

**What's Working:**
- VulkanRenderBackend initialization and device creation
- GPUBuffer and GPUTexture creation with VMA
- Window and swapchain management
- Basic pipeline creation (with and without graphics pipeline library)
- GPUGraph execution framework
- Resource barrier generation
- Render pass recording with framebuffer creation
- Compute pass recording
- Transfer pass with staging buffer uploads
- Window presentation with semaphores
- Shader module caching (T085)
- Pipeline layout creation from shader reflection (T086)
- Descriptor set layout creation (T087)
- Framebuffer/renderpass caching (T088)
- Staging buffer cleanup (T089)
- Async compute queue support (T090)
- FR-013 compliant ShaderImporter using IShaderCompilerManager (T091-T094)

### Next Steps

1. **T079**: Update VulkanRendererBackendTester to validate all user stories
2. **T082**: Run quickstart.md validation scenarios
3. **T083**: Verify 30%+ pipeline creation speedup with graphics pipeline library
4. **T084**: Verify 60+ fps performance with VulkanRendererBackendTester

### MVP First (User Stories 1, 2, 5 Only)

1. ~~Complete Phase 1: Setup~~ ✅
2. ~~Complete Phase 2: Foundational (CRITICAL - blocks all stories)~~ ✅
3. ~~Complete Phase 3: User Story 1 (Core Interface)~~ ✅
4. ~~Complete Phase 4: User Story 2 (Resources)~~ ✅
5. ~~Complete Phase 5: User Story 5 (Window/Swapchain)~~ ✅
6. **STOP and VALIDATE**: Test triangle rendering with VulkanRendererBackendTester
7. Deploy/demo if ready

### Incremental Delivery

1. ~~Complete Setup + Foundational → Foundation ready~~ ✅
2. ~~Add User Story 1 → Interface methods callable~~ ✅
3. ~~Add User Story 2 → GPU resources work~~ ✅
4. ~~Add User Story 5 → Can display to screen (MVP complete - can render triangle!)~~ ✅
5. ~~Add User Story 3 → Pipeline optimization~~ ✅
6. ~~Add User Story 4 → Full GPUGraph execution~~ ✅ (core)
7. ~~Complete US4 shader integration → Full rendering pipeline~~ ✅
8. Run validation tests (T079, T082-T084) → Production ready
9. Each story adds value without breaking previous stories

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
