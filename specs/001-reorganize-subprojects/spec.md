# Feature Specification: Reorganize Subprojects

**Feature Branch**: `001-reorganize-subprojects`
**Created**: 2026-03-22
**Status**: Draft
**Input**: User description: "我要对子项目文件做一些修改：移动项目DotNetHost到新建的Experimental文件夹下；将CoreTests,D3D12RenderBackendTester,VulkanRendererBackendTester移到新建的Test文件夹下；将RenderInterface,ShaderCompiler,IOManager,TimerSystem移动到新建的Interface文件夹下"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Create Experimental Folder and Move DotNetHost (Priority: P1)

As a developer, I want the DotNetHost project to be organized under an Experimental folder so that experimental/in-progress features are clearly separated from stable code.

**Why this priority**: Establishing the Experimental folder first allows for clear categorization of projects that are not yet production-ready.

**Independent Test**: Can be fully tested by verifying that the Experimental folder exists and DotNetHost is located within it, with all project references intact.

**Acceptance Scenarios**:

1. **Given** the project root directory, **When** reorganization is complete, **Then** an `Experimental/` folder exists containing the `DotNetHost/` project
2. **Given** the DotNetHost project is moved, **When** the solution is opened, **Then** all project references and dependencies load correctly
3. **Given** the reorganization is complete, **When** building the project, **Then** it compiles without errors

---

### User Story 2 - Create Test Folder and Move Test Projects (Priority: P1)

As a developer, I want all test-related projects (CoreTests, D3D12RenderBackendTester, VulkanRendererBackendTester) to be organized under a Test folder so that test projects are clearly separated from production code.

**Why this priority**: Organizing test projects together improves project navigation and makes the codebase structure clearer for all team members.

**Independent Test**: Can be fully tested by verifying that the Test folder exists and contains all three test projects with correct references.

**Acceptance Scenarios**:

1. **Given** the project root directory, **When** reorganization is complete, **Then** a `Test/` folder exists containing `CoreTests/`, `D3D12RenderBackendTester/`, and `VulkanRendererBackendTester/`
2. **Given** the test projects are moved, **When** the solution is opened, **Then** all project references load correctly
3. **Given** the reorganization is complete, **When** running tests, **Then** all tests execute and pass as expected

---

### User Story 3 - Create Interface Folder and Move Interface Projects (Priority: P1)

As a developer, I want all interface/abstract projects (RenderInterface, ShaderCompiler, IOManager, TimerSystem) to be organized under an Interface folder so that core abstractions are clearly separated from implementations.

**Why this priority**: Interface modules define contracts that implementations depend on; organizing them together clarifies the architecture.

**Independent Test**: Can be fully tested by verifying that the Interface folder exists and contains all four interface projects with correct references.

**Acceptance Scenarios**:

1. **Given** the project root directory, **When** reorganization is complete, **Then** an `Interface/` folder exists containing `RenderInterface/`, `ShaderCompiler/`, `IOManager/`, and `TimerSystem/`
2. **Given** the interface projects are moved, **When** the solution is opened, **Then** all project references load correctly
3. **Given** the reorganization is complete, **When** building the project, **Then** it compiles without errors

---

### User Story 4 - Update Build Configuration (Priority: P2)

As a developer, I want all build configuration files (CMakeLists.txt, etc.) to be updated to reflect the new project structure so that the build system continues to work correctly.

**Why this priority**: Without updated build configurations, the project cannot be compiled after the move.

**Independent Test**: Can be fully tested by running a clean build and verifying it completes successfully.

**Acceptance Scenarios**:

1. **Given** the projects are moved, **When** CMake configuration is run, **Then** all projects are discovered and configured correctly
2. **Given** the build configuration is updated, **When** building the entire solution, **Then** all projects compile without path-related errors

---

### Edge Cases

- What happens when external references point to the old project paths? → All references must be updated to point to new locations
- How does the system handle relative paths in configuration files? → All relative paths must be recalculated based on new folder depth
- What if there are hardcoded paths in source files? → Hardcoded paths should be identified and updated or replaced with relative paths

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST create a new folder named `Experimental` at the project root level
- **FR-002**: System MUST move the `DotNetHost` project folder into the `Experimental` folder
- **FR-003**: System MUST create a new folder named `Test` at the project root level
- **FR-004**: System MUST move `CoreTests`, `D3D12RenderBackendTester`, and `VulkanRendererBackendTester` project folders into the `Test` folder
- **FR-005**: System MUST create a new folder named `Interface` at the project root level
- **FR-006**: System MUST move `RenderInterface`, `ShaderCompiler`, `IOManager`, and `TimerSystem` project folders into the `Interface` folder
- **FR-007**: All project references in CMakeLists.txt files MUST be updated to reflect new paths
- **FR-008**: All relative paths in configuration files MUST be adjusted for the new folder depth
- **FR-009**: The solution/build MUST compile successfully after reorganization

### Key Entities

- **Experimental Folder**: New directory at root level to house experimental/in-progress projects
- **Test Folder**: New directory at root level to house all test-related projects
- **Interface Folder**: New directory at root level to house core interface/abstract modules
- **DotNetHost Project**: Experimental .NET hosting project to be moved to Experimental/
- **CoreTests Project**: Core functionality test project to be moved to Test/
- **D3D12RenderBackendTester Project**: D3D12 backend test project to be moved to Test/
- **VulkanRendererBackendTester Project**: Vulkan backend test project to be moved to Test/
- **RenderInterface Project**: Rendering abstraction interface to be moved to Interface/
- **ShaderCompiler Project**: Shader compilation interface to be moved to Interface/
- **IOManager Project**: I/O management interface to be moved to Interface/
- **TimerSystem Project**: Timer system interface to be moved to Interface/

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Project structure reflects the new organization with `Experimental/DotNetHost/`, `Test/` containing all three test projects, and `Interface/` containing all four interface projects
- **SC-002**: Full solution build completes with zero path-related errors
- **SC-003**: All existing tests pass after reorganization
- **SC-004**: No hardcoded paths remain pointing to old project locations

## Assumptions

- The project uses CMake as its build system (based on presence of CMakeLists.txt)
- All projects are located directly under the root folder currently
- No other tools or scripts depend on the current project paths
- Git history will be preserved during the move (using git mv)
