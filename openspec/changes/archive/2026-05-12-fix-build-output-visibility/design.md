## Context

`build.py` 当前通过以下流程运行 cmake：

1. Python 生成一个 `.bat` 文件（设置 PATH → 调用 vcvars64.bat → 运行 cmake）
2. `subprocess.run(["cmd", "//c", str(bat)])` 启动 cmd.exe 执行该 bat
3. Python 在 MSYS2 bash 环境中运行

在步骤 2 中，cmd.exe 的 stdout/stderr 理应继承 Python 的 stdout/stderr（未设置 `capture_output`），进而透传到 bash 终端。但实际测试表明 cmake 的 configure/build 输出（如 "Building CXX object..."、"FAILED:" 等）完全不可见。只有 cmd.exe 的启动横幅（"Microsoft Windows [Version ...]"）和 `@echo off` 之前的行可见。

根本原因可能是 MSYS2 bash 对 Python 子进程的 stdout 链路有某种截断或缓冲问题，导致第三层进程（Python → cmd.exe → cmake/ninja）的输出无法到达终端。

另外，`main()` 函数末尾无条件打印 `BUILD SUCCESSFUL`，即使 `cmake --build` 实际失败也会显示成功。

## Goals / Non-Goals

**Goals:**
- 用户运行 `python build.py` 时能看到 cmake configure 和 ninja build 的实时输出（或至少在完成后看到完整日志）
- `build.py` 能根据 cmake 实际退出码报告构建成功或失败
- 不再出现"实际失败但显示 SUCCESSFUL"的误导

**Non-Goals:**
- 不改变构建流程本身（仍使用 bat 文件 + vcvars + cmake --preset）
- 不改变 `build.py` 的用户接口（仍支持 `--no-configure` 和 target 参数）
- 不处理网络/依赖下载问题

## Decisions

### Decision 1: 在 bat 文件中将 cmake 输出重定向到日志文件

**选择**：修改 `_write_bat()` 生成的 bat 文件，将 cmake 的 stdout 和 stderr 重定向到项目根目录的临时日志文件（如 `_build_output.log`）。

**理由**：
- 完全绕过 MSYS2/bash 对子进程输出的捕获问题
- 简单可靠，不需要修改 `subprocess.run` 的调用方式
- bat 文件已经有 `@echo off` 和 PATH 设置，加一行 `>> log 2>&1` 即可

**Alternatives considered**：
- **`capture_output=True` + 打印**：`subprocess.run(capture_output=True)` 只捕获 cmd.exe 自身输出，cmake 在 bat 内部的输出不一定在 `result.stdout` 中
- **直接运行 cmake（不用 bat）**：需要手动设置 MSVC 环境变量，复杂且容易遗漏（Windows SDK 路径、LIB、INCLUDE 等）
- **使用 `subprocess.Popen` + 实时读取**：仍受 stdout 链路问题影响

### Decision 2: 保留实时输出体验

**选择**：在 bat 文件中使用 `tee` 等效方式（`>> log 2>&1` + 正常 stdout）或直接只用日志文件。

由于 Windows cmd.exe 没有 `tee` 命令，且主要目标是能看到错误信息，选择：仅重定向到日志文件，Python 在 `subprocess.run` 返回后读取并打印日志内容。这在 ms 级延迟上是"事后"输出，但用户能完整看到 cmake 输出。

### Decision 3: Configure 和 Build 使用独立日志文件

**选择**：`_write_bat()` 接受一个可选的 `log_file` 参数来指定日志文件名。configure 阶段使用 `_build_configure.log`，build 阶段使用 `_build_build.log`。`run_msvc()` 在 `subprocess.run` 返回后读取并打印日志，随后删除。

**理由**：
- `main()` 中 configure 和 build 各调用一次 `run_msvc()`，如果共用同一个日志文件，第二次调用会覆盖第一次的输出
- 独立文件确保用户看到完整的 configure 输出 + 完整的 build 输出

### Decision 4: Bat 文件显式传递退出码

**选择**：在 bat 文件中 cmake 命令之后添加 `exit /b %ERRORLEVEL%`，确保 cmake 的退出码能可靠传递回 Python 的 `subprocess.run`。

**理由**：
- 虽然 cmd.exe 默认返回最后一条命令的退出码，但在某些 MSYS2 交互场景下，cmd.exe 可能返回 0 即使内部命令失败
- 显式 `exit /b %ERRORLEVEL%` 消除歧义，是防御性措施

### Decision 5: 移除无条件的 BUILD SUCCESSFUL（确认现有行为）

**选择**：`main()` 末尾保留 `BUILD SUCCESSFUL`，不做逻辑修改。

**理由**：
- 当前代码中 `run_msvc()` 在检测到非零退出码时通过 `sys.exit()` 终止进程，`main()` 末尾的 `BUILD SUCCESSFUL` 在失败时根本不可达
- 因此该消息并非"无条件"——它已经是条件性的（仅成功时可达）
- 配合 Decision 4 加固退出码传递后，此行为更加可靠
- 无需额外修改代码逻辑，只需确保日志输出在 `sys.exit()` 之前完成

## Risks / Trade-offs

- [日志文件残留] → Python 在打印日志后立即删除日志文件，异常退出时 bat 文件也会保留（已有机制）
- [日志文件很大] → 正常构建日志通常 < 5MB，可接受
- [非实时输出] → 用户看不到逐行滚动的编译进度。可接受，因为主要需求是判断成功/失败和查看错误
- [日志文件未生成] → 如果 bat 文件在重定向输出之前就失败（如 cmd.exe 无法启动），日志文件不存在。Python 需处理文件不存在的情况：打印警告并使用已有的退出码信息
