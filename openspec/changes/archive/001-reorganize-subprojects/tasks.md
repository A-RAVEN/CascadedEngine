# Tasks: Reorganize Subprojects

**Change ID**: 001-reorganize-subprojects
**Status**: Completed

---

## Format: `[ID] [Status] Description`

- **Status**: [X] Complete, [ ] Pending

---

## Phase 1: Create Directory Structure

- [X] T001 Create `Experimental/` directory at project root
- [X] T002 Create `Test/` directory at project root
- [X] T003 Create `Interface/` directory at project root

---

## Phase 2: Move Experimental Projects

- [X] T004 Move `DotNetHost/` to `Experimental/DotNetHost/`
- [X] T005 Update `Experimental/DotNetHost/CMakeLists.txt` for new path depth

---

## Phase 3: Move Test Projects

- [X] T006 Move `CoreTests/` to `Test/CoreTests/`
- [X] T007 Move `D3D12RenderBackendTester/` to `Test/D3D12RenderBackendTester/`
- [X] T008 Move `VulkanRendererBackendTester/` to `Test/VulkanRendererBackendTester/`
- [X] T009 Update all test project CMakeLists.txt for new paths

---

## Phase 4: Move Interface Projects

- [X] T010 Move `RenderInterface/` to `Interface/RenderInterface/`
- [X] T011 Move `ShaderCompiler/` to `Interface/ShaderCompiler/`
- [X] T012 Move `IOManager/` to `Interface/IOManager/`
- [X] T013 Move `TimerSystem/` to `Interface/TimerSystem/`
- [X] T014 Update all interface project CMakeLists.txt for new paths

---

## Phase 5: Update Build Configuration

- [X] T015 Update root `CMakeLists.txt` with new subdirectory paths
- [X] T016 Update all `add_subdirectory()` calls
- [X] T017 Update all `target_include_directories()` relative paths
- [X] T018 Update all `target_link_libraries()` dependency references

---

## Phase 6: Verification

- [X] T019 Run CMake configuration - verify all projects discovered
- [X] T020 Build entire solution - verify zero path-related errors
- [X] T021 Run all existing tests - verify all pass
- [X] T022 Search for hardcoded old paths - verify none remain

---

## Progress Summary

**Total Tasks**: 22
**Completed**: 22
**Remaining**: 0

✅ **All tasks completed successfully**
