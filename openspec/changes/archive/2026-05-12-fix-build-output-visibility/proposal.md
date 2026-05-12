## Why

`build.py` 通过 `subprocess.run(["cmd", "//c", str(bat)])` 在 cmd.exe 内部运行 cmake configure 和 build。在 MSYS2/bash 环境下，cmd.exe 子进程的标准输出和标准错误无法被外层捕获，导致用户看不到任何 cmake 配置/编译输出。更严重的是，`main()` 末尾无条件打印 `BUILD SUCCESSFUL`，即使 cmake configure 或 build 实际失败了也会显示成功，产生严重误导。

## What Changes

- **捕获 cmake 输出**：让 cmake configure 和 build 的输出能透传到用户的终端，以便实时看到编译进度和错误信息
- **真实成功/失败判断**：在 bat 文件中显式传递 cmake 退出码（`exit /b %ERRORLEVEL%`），配合 `run_msvc()` 中已有的 `sys.exit` 检查，确保构建失败时不会误报成功

## Capabilities

### New Capabilities
- `build-output-visible`: build.py 能够将 cmake configure 和 build 的实际输出展示给用户，并能根据退出码正确报告成功或失败

### Modified Capabilities
- 无

## Impact

- `build.py`：`_write_bat()`, `run_msvc()`, `configure()`, `build()`, `main()` 函数需修改
- 无 API 影响，无依赖变更
