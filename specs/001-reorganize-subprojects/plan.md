# Implementation Plan: Reorganize Subprojects

**Branch**: `001-reorganize-subprojects` | **Date**: 2026-03-22 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/001-reorganize-subprojects/spec.md`

## Summary

Reorganize project structure by creating three new folders (`Experimental/`, `Test/`, `Interface/`) and moving 8 existing projects into them. This is a file system and build configuration change with no code modifications required.

## Technical Context

**Language/Version**: C++ (CMake build system)
**Primary Dependencies**: CMake 3.x, vcpkg (for external dependencies)
**Storage**: N/A (file system reorganization only)
**Testing**: Existing test projects (CoreTests, D3D12RenderBackendTester, VulkanRendererBackendTester)
**Target Platform**: Windows x64
**Project Type**: Build system / project structure reorganization
**Performance Goals**: N/A
**Constraints**: Git history must be preserved (use `git mv`), build must succeed after reorganization
**Scale/Scope**: 8 projects to move, 3 new folders to create

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Evidence |
|-----------|--------|----------|
| I. 模块优先的引擎架构 | ✅ Pass | Moving existing modules, not creating new ones; module boundaries unchanged |
| II. 以契约驱动的后端一致性 | ✅ Pass | Interface modules being grouped together improves contract visibility |
| III. 可确定复现的资产与着色器流水线 | ✅ Pass | Build system changes maintain reproducibility |
| IV. 测试与验证门禁 | ✅ Pass | SC-003 requires all tests pass after reorganization |
| V. 性能预算与可调试性 | ✅ Pass | No runtime changes |

**Gate Result**: ✅ PASS - All principles satisfied

## Project Structure

### Documentation (this feature)

```text
specs/001-reorganize-subprojects/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root) - Current vs Target

**Current Structure:**
```text
CascadedEngine/
├── CoreTests/
├── D3D12RenderBackend/
├── D3D12RenderBackendTester/
├── DotNetHost/
├── IMGUIContext/
├── IOManager/
├── IOManager_FS/
├── RenderInterface/
├── ShaderCompiler/
├── ShaderCompilerSlang/
├── ShaderProcessor/
├── ThreadManager/
├── TimerSystem/
├── TimerSystem_Impl/
├── VulkanRenderBackend/
├── VulkanRenderBackendNew/
├── VulkanRendererBackendTester/
├── WindowSystem/
├── CACore/
├── CAResources/
├── GeneralResources/
├── ExternalLib/
├── EditorConfigs/
├── CMake/
├── CMakeLists.txt
└── CMakePresets.json
```

**Target Structure:**
```text
CascadedEngine/
├── Experimental/
│   └── DotNetHost/
├── Test/
│   ├── CoreTests/
│   ├── D3D12RenderBackendTester/
│   └── VulkanRendererBackendTester/
├── Interface/
│   ├── RenderInterface/
│   ├── ShaderCompiler/
│   ├── IOManager/
│   └── TimerSystem/
├── D3D12RenderBackend/
├── IMGUIContext/
├── IOManager_FS/
├── ShaderCompilerSlang/
├── ShaderProcessor/
├── ThreadManager/
├── TimerSystem_Impl/
├── VulkanRenderBackend/
├── VulkanRenderBackendNew/
├── WindowSystem/
├── CACore/
├── CAResources/
├── GeneralResources/
├── ExternalLib/
├── EditorConfigs/
├── CMake/
├── CMakeLists.txt
└── CMakePresets.json
```

**Structure Decision**: Group related projects by purpose:
- `Experimental/` - In-progress/experimental projects
- `Test/` - All test projects
- `Interface/` - Core abstraction/interface modules

## Complexity Tracking

> No violations - plan adheres to all constitutional principles.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| N/A | N/A | N/A |
