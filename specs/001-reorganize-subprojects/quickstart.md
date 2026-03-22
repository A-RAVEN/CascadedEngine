# Quickstart: Reorganize Subprojects

**Feature**: 001-reorganize-subprojects
**Date**: 2026-03-22

## Overview

This guide provides the execution steps for reorganizing the project structure.

## Prerequisites

- Git repository is clean (no uncommitted changes)
- Current branch: `001-reorganize-subprojects`
- Build environment is set up (CMake, compiler)

## Execution Steps

### Step 1: Create Interface Folder and Move Projects

```bash
# Create folder
mkdir Interface

# Move projects (preserves git history)
git mv RenderInterface Interface/
git mv ShaderCompiler Interface/
git mv IOManager Interface/
git mv TimerSystem Interface/
```

### Step 2: Create Test Folder and Move Projects

```bash
# Create folder
mkdir Test

# Move projects
git mv CoreTests Test/
git mv D3D12RenderBackendTester Test/
git mv VulkanRendererBackendTester Test/
```

### Step 3: Create Experimental Folder and Move Project

```bash
# Create folder
mkdir Experimental

# Move project
git mv DotNetHost Experimental/
```

### Step 4: Update CMakeLists.txt

Update the root `CMakeLists.txt` to reflect new paths:

```cmake
# Interface modules
add_subdirectory(Interface/RenderInterface)
add_subdirectory(Interface/ShaderCompiler)
add_subdirectory(Interface/IOManager)
add_subdirectory(Interface/TimerSystem)

# Test projects
add_subdirectory(Test/CoreTests)
add_subdirectory(Test/D3D12RenderBackendTester)
add_subdirectory(Test/VulkanRendererBackendTester)

# Experimental projects
add_subdirectory(Experimental/DotNetHost)
```

### Step 5: Verify Build

```bash
# Configure
cmake --preset=x64-relWithDebugInfo

# Build
cmake --build out/build/x64-relWithDebugInfo
```

## Verification Checklist

- [ ] `Interface/` folder exists with 4 projects
- [ ] `Test/` folder exists with 3 projects
- [ ] `Experimental/` folder exists with 1 project
- [ ] No projects remain in original root locations
- [ ] CMake configuration succeeds
- [ ] Build completes without errors
- [ ] Tests pass

## Rollback (if needed)

```bash
# Move everything back
git mv Interface/* .
git mv Test/* .
git mv Experimental/* .

# Remove empty folders
rmdir Interface Test Experimental

# Revert CMakeLists.txt changes
git checkout CMakeLists.txt
```
