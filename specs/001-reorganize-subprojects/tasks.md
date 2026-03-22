# Tasks: Reorganize Subprojects

**Input**: Design documents from `/specs/001-reorganize-subprojects/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, quickstart.md

**Tests**: Not explicitly requested - implementation tasks only.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Verify clean working state before reorganization

- [x] T001 Verify git working directory is clean (no uncommitted changes)
- [x] T002 Verify current branch is `001-reorganize-subprojects`
- [ ] T003 Verify build succeeds before reorganization by running `cmake --preset=x64-relWithDebugInfo`

**Checkpoint**: Working directory clean, build succeeds

---

## Phase 2: User Story 1 - Experimental Folder (Priority: P1) 🎯 MVP

**Goal**: Create Experimental folder and move DotNetHost project

**Independent Test**: Verify `Experimental/DotNetHost/` exists and project references load

### Implementation for User Story 1

- [x] T004 [US1] Create `Experimental/` folder at repository root
- [x] T005 [US1] Move DotNetHost to Experimental folder using `git mv DotNetHost Experimental/`

**Checkpoint**: Experimental folder with DotNetHost exists ✅

---

## Phase 3: User Story 2 - Test Folder (Priority: P1)

**Goal**: Create Test folder and move all test projects

**Independent Test**: Verify `Test/` contains all three test projects with correct references

### Implementation for User Story 2

- [x] T006 [US2] Create `Test/` folder at repository root
- [x] T007 [P] [US2] Move CoreTests using `git mv CoreTests Test/`
- [x] T008 [P] [US2] Move D3D12RenderBackendTester using `git mv D3D12RenderBackendTester Test/`
- [x] T009 [P] [US2] Move VulkanRendererBackendTester using `git mv VulkanRendererBackendTester Test/`

**Checkpoint**: Test folder with all three test projects exists ✅

---

## Phase 4: User Story 3 - Interface Folder (Priority: P1)

**Goal**: Create Interface folder and move all interface projects

**Independent Test**: Verify `Interface/` contains all four interface projects with correct references

### Implementation for User Story 3

- [x] T010 [US3] Create `Interface/` folder at repository root
- [x] T011 [P] [US3] Move RenderInterface using `git mv RenderInterface Interface/`
- [x] T012 [P] [US3] Move ShaderCompiler using `git mv ShaderCompiler Interface/`
- [x] T013 [P] [US3] Move IOManager using `git mv IOManager Interface/`
- [x] T014 [P] [US3] Move TimerSystem using `git mv TimerSystem Interface/`

**Checkpoint**: Interface folder with all four interface projects exists ✅

---

## Phase 5: User Story 4 - Build Configuration (Priority: P2)

**Goal**: Update all CMake configuration to reflect new project structure

**Independent Test**: Run clean build and verify it completes without path-related errors

### Implementation for User Story 4

- [x] T015 [US4] Update root CMakeLists.txt paths for Interface projects in `CMakeLists.txt`
- [x] T016 [US4] Update root CMakeLists.txt paths for Test projects in `CMakeLists.txt`
- [x] T017 [US4] Update root CMakeLists.txt paths for Experimental projects in `CMakeLists.txt`
- [x] T018 [US4] Search and update any other files with hardcoded project paths

**Checkpoint**: All CMake paths updated ✅

---

## Phase 6: Verification & Polish

**Purpose**: Verify complete reorganization success

- [ ] T019 Run CMake configure: `cmake --preset=x64-relWithDebugInfo` (blocked: Ninja not in PATH)
- [ ] T020 Run full build: `cmake --build out/build/x64-relWithDebugInfo`
- [x] T021 Verify folder structure matches target in plan.md
- [ ] T022 Run quickstart.md validation checklist
- [ ] T023 Commit changes with message: `refactor: reorganize subprojects into Experimental/Test/Interface folders`

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - verify clean state first
- **US1-US3 (Phases 2-4)**: Can run in parallel - different projects, no conflicts
- **Build Config (Phase 5)**: Depends on US1, US2, US3 completion
- **Verification (Phase 6)**: Depends on all previous phases

### User Story Dependencies

- **User Story 1 (Experimental)**: Independent - no dependencies
- **User Story 2 (Test)**: Independent - no dependencies
- **User Story 3 (Interface)**: Independent - no dependencies
- **User Story 4 (Build Config)**: Depends on US1, US2, US3 - must run after moves complete

### Within Each User Story

- Create folder before moving projects
- All moves within a story can run in parallel (marked [P])

### Parallel Opportunities

- US1, US2, US3 can all run in parallel (different projects)
- Within US2: T007, T008, T009 can run in parallel
- Within US3: T011, T012, T013, T014 can run in parallel

---

## Implementation Status

| Phase | Status | Notes |
|-------|--------|-------|
| Phase 1: Setup | ✅ Complete | T003 skipped (env issue) |
| Phase 2: US1 Experimental | ✅ Complete | |
| Phase 3: US2 Test | ✅ Complete | |
| Phase 4: US3 Interface | ✅ Complete | |
| Phase 5: Build Config | ✅ Complete | |
| Phase 6: Verification | ⏳ Pending | T019-T020 blocked by Ninja |

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- All moves use `git mv` to preserve history
- Commit after Phase 6 verification passes
- Rollback: Use `git mv` to move projects back if needed
