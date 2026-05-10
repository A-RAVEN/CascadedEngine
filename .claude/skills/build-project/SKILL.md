---
name: build-project
description: Build the CascadedEngine project using build.py. Invoked via /build command. Triggers on user requests like "build the project", "compile", "/build".
license: MIT
metadata:
  author: cascaded-engine
  version: "1.0"
---

# Build Project

Execute the CascadedEngine build script.

## When to Use

- User explicitly asks to build, compile, or construct the project
- User types `/build`
- User asks if the project compiles

**Do NOT** build automatically — only when the user explicitly requests it.

## Steps

### 1. Verify build.py exists

Check `E:\Projects\CascadedEngine\build.py` exists. If missing, report the error and suggest re-creating it.

### 2. Run build

```bash
python build.py 2>&1
```

Timeout: 600000ms (10 minutes) — the full build with all external dependencies can take a while.

### 3. Interpret results

- **Exit code 0**: Report "构建成功" and list the key outputs (DLLs, EXEs) from `out/build/x64-relWithDebugInfo/bin/`
- **Exit code non-zero**: Extract and report the error lines (grep for `FAILED:`, `error C`, `error LNK`, `fatal error`, `ERROR:`)
- **"BUILD SUCCESSFUL" in output**: Build completed successfully
- **"ERROR: Build failed"**: Build failed — extract the relevant error messages

### 4. Key outputs

After successful build, these files should exist:
- `bin/VulkanRenderBackend.dll`
- `bin/D3D12RenderBackend.dll`
- `bin/CoreTests.exe`
- `bin/D3D12RendererBackendTester.exe`

### Incremental builds

If the user only modified a few files, suggest building only the relevant target instead of the full build:
```bash
cmake --build out/build/x64-relWithDebugInfo --target VulkanRenderBackend
```

This requires the VS x64 environment and ninja in PATH (see build.py for setup).
