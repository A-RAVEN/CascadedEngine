# Data Model: Reorganize Subprojects

**Feature**: 001-reorganize-subprojects
**Date**: 2026-03-22

## Overview

This feature does not introduce new data entities. It reorganizes existing project structure. This document describes the structural mapping.

## Folder Entities

### Experimental Folder

| Attribute | Value |
|-----------|-------|
| Purpose | House experimental/in-progress projects |
| Location | `/Experimental/` (root level) |
| Contains | `DotNetHost/` |

### Test Folder

| Attribute | Value |
|-----------|-------|
| Purpose | House all test-related projects |
| Location | `/Test/` (root level) |
| Contains | `CoreTests/`, `D3D12RenderBackendTester/`, `VulkanRendererBackendTester/` |

### Interface Folder

| Attribute | Value |
|-----------|-------|
| Purpose | House core abstraction/interface modules |
| Location | `/Interface/` (root level) |
| Contains | `RenderInterface/`, `ShaderCompiler/`, `IOManager/`, `TimerSystem/` |

## Project Mapping

| Project | Current Location | Target Location | Type |
|---------|-----------------|-----------------|------|
| DotNetHost | `/DotNetHost/` | `/Experimental/DotNetHost/` | Experimental |
| CoreTests | `/CoreTests/` | `/Test/CoreTests/` | Test |
| D3D12RenderBackendTester | `/D3D12RenderBackendTester/` | `/Test/D3D12RenderBackendTester/` | Test |
| VulkanRendererBackendTester | `/VulkanRendererBackendTester/` | `/Test/VulkanRendererBackendTester/` | Test |
| RenderInterface | `/RenderInterface/` | `/Interface/RenderInterface/` | Interface |
| ShaderCompiler | `/ShaderCompiler/` | `/Interface/ShaderCompiler/` | Interface |
| IOManager | `/IOManager/` | `/Interface/IOManager/` | Interface |
| TimerSystem | `/TimerSystem/` | `/Interface/TimerSystem/` | Interface |

## State Transitions

```
[Root Directory]
       │
       ├── DotNetHost ──────────────────→ [Experimental/DotNetHost]
       │
       ├── CoreTests ───────────────────→ [Test/CoreTests]
       ├── D3D12RenderBackendTester ────→ [Test/D3D12RenderBackendTester]
       ├── VulkanRendererBackendTester ─→ [Test/VulkanRendererBackendTester]
       │
       ├── RenderInterface ─────────────→ [Interface/RenderInterface]
       ├── ShaderCompiler ──────────────→ [Interface/ShaderCompiler]
       ├── IOManager ───────────────────→ [Interface/IOManager]
       └── TimerSystem ─────────────────→ [Interface/TimerSystem]
```

## Validation Rules

1. All 8 projects must exist in their target locations after move
2. No projects should remain in original root locations
3. All CMake target references must resolve correctly
4. Build must succeed after reorganization
