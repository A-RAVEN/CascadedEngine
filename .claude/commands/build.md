---
name: Build Project
description: Compile the CascadedEngine project (configure + build all targets)
category: Build
tags: [build, compile, ninja, cmake]
---

# Build Project

Run `build.bat` to configure CMake and compile all targets.

## Usage

```
/build
```

## What It Does

1. Detects Visual Studio installation (2026 or 2022)
2. Sets up x64 build environment via vcvars64.bat
3. Runs `cmake --preset x64-relWithDebugInfo` to configure
4. Patches generated build.ninja for CMake 4.x compatibility
5. Runs `cmake --build` to compile all targets

The build produces DLLs, EXEs, and static libraries in `out/build/x64-relWithDebugInfo/bin/`.
