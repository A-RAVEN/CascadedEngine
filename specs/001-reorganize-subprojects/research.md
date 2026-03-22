# Research: Reorganize Subprojects

**Feature**: 001-reorganize-subprojects
**Date**: 2026-03-22

## Research Questions

### 1. CMake Path Update Strategy

**Decision**: Use `add_subdirectory()` with updated relative paths

**Rationale**:
- CMake's `add_subdirectory()` accepts relative paths from the current CMakeLists.txt
- Moving projects one level deeper requires adding `../` prefix adjustments
- Root CMakeLists.txt will need path updates from `project_name` to `Folder/project_name`

**Alternatives Considered**:
- Creating separate CMakeLists.txt in each new folder (rejected - adds unnecessary indirection)
- Using absolute paths (rejected - breaks portability)

### 2. Git History Preservation

**Decision**: Use `git mv` for all moves

**Rationale**:
- `git mv` preserves file history across the move
- Standard Git best practice for refactoring

**Alternatives Considered**:
- Manual copy + delete (rejected - loses history)
- `git mv` with `--follow` flag later (unnecessary if using `git mv` from start)

### 3. Move Order

**Decision**: Create folders first, then move projects in dependency order

**Rationale**:
- Interface projects have no dependencies on other projects in this repo
- Test projects depend on Interface projects
- Experimental projects (DotNetHost) are isolated

**Move Order**:
1. Create `Interface/` folder
2. Move `RenderInterface`, `ShaderCompiler`, `IOManager`, `TimerSystem` → `Interface/`
3. Create `Test/` folder
4. Move `CoreTests`, `D3D12RenderBackendTester`, `VulkanRendererBackendTester` → `Test/`
5. Create `Experimental/` folder
6. Move `DotNetHost` → `Experimental/`
7. Update root `CMakeLists.txt` paths

### 4. Build Verification

**Decision**: Run CMake configure + build after each folder group move

**Rationale**:
- Incremental verification catches issues early
- Easier to debug if only one folder group was changed

**Verification Steps**:
1. After Interface moves: `cmake --preset=x64-relWithDebugInfo && cmake --build out/build/x64-relWithDebugInfo`
2. After Test moves: Rebuild and run tests
3. After Experimental moves: Final full rebuild

## Resolved Clarifications

All technical questions resolved - no NEEDS CLARIFICATION items remain.
