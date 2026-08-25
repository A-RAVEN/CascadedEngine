# CascadedEngine Development Guidelines

Auto-generated from all feature plans. Last updated: 2026-03-22

## Active Technologies
- C++20 (CMake 3.12+, MSVC toolchain) (002-vulkan-backend-renderinterface)
- N/A (GPU memory via VMA, no persistent storage) (002-vulkan-backend-renderinterface)

- C++ (CMake build system) + CMake 3.x, vcpkg (for external dependencies) (001-reorganize-subprojects)

## Project Structure

```text
src/
tests/
```

## Commands

- `/commit-from-propose`: 根据 OpenSpec proposal 和本地 git diff 生成中文 commit message 并提交。可选指定 change name

# Add commands for C++ (CMake build system)

## Code Style

C++ (CMake build system): Follow standard conventions

## Recent Changes
- 002-vulkan-backend-renderinterface: Added C++20 (CMake 3.12+, MSVC toolchain)

- 001-reorganize-subprojects: Added C++ (CMake build system) + CMake 3.x, vcpkg (for external dependencies)

<!-- MANUAL ADDITIONS START -->
## 工作规则

- **禁止编译/配置**: 除非用户明确说明，不要尝试配置或编译此项目。但如果当前 OpenSpec change 的 tasks 中明确包含编译验证步骤（如 `build.py`），可直接执行无需征询同意
- **禁止手动敲构建命令**: 构建只允许通过项目脚本（`python build.py` 等）执行，禁止手动敲 `ninja`/`cmake --build`/`cmake -S` 等构建命令。手动命令可以只构建部分目标（如只构建 tester 不构建被动态加载的 DLL），产生"测试跑的是旧产物"的不一致。脚本（build.py）保证全量构建所有目标。若现有脚本不满足需求（如缺少 Debug 配置），应扩展脚本而不是绕过脚本。
- **禁止 sleep 轮询异步任务**: Workflow/Agent/Bash 异步任务完成后会自动发送通知，禁止用 `sleep N` 循环轮询等待
- **审查闭环**: tasks 中最后一个任务必须是审查任务（Review & Adversarial Verify）。对刚完成的所有任务做正确性/完整性/诚实性审查，需开 workflow 做对抗验证。验证发现的新问题必须添加为新的 task（附加 `[AUDIT]` 前缀），然后追加一个新的审查任务，继续执行。循环直到审查无新问题或达到 3 轮。**审查必须做回归检查与去重**：每轮审查必须先核对上一轮已修复的问题是否在本轮回归（修复是否真正生效、是否被后续改动覆盖）；新增 [AUDIT] task 前必须先与 Review Log 中已处理的问题去重（按根因而非表象/行号判断同一问题），同一问题不得重复开 task 反复修改——若确认是上一轮已处理问题的回归，审查重点应转为"为何修复未生效/被覆盖"并补上缺失的验证，而不是推翻重改。审查结果写入 tasks.md 末尾的 `## Review Log` 区域。**重要：即使达到 3 轮上限停止，停下的那一刻交付的必须是审查结果报告，而不是"完成了第 N 个任务"之类的实现进度汇报。用户看到的最后一个输出应该是 Review Log。**
- **所有 bug 默认是我的**: 禁止使用 "pre-existing bug"、"已有问题"、"之前就存在" 等表述。任何 crash、错误、异常行为默认认为是我的改动引入的，除非完成 git stash + 重新编译 + 测试对比后仍未排除。即使经过验证，也不得使用 "pre-existing" 措辞——改用 "baseline 中同样存在" 等事实描述。
- **审查必须做 API 外部正确性验证**: 审查任务中，对代码中调用的每个外部 API（Vulkan/vk.hpp、VMA、slang、GLFW 等）必须联网搜索官方文档确认：
  - 参数类型/个数/顺序是否正确
  - 枚举值、flag 组合是否合法
  - 返回值和错误处理是否符合 API 语义
  - 使用前提（扩展启用、feature 开启、对象生命周期）是否满足
  审查结果中必须**引用 API 文档的网址**（或本地文档路径），格式如 `[VkCmdPipelineBarrier](https://docs.vulkan.org/spec/latest/chapters/synchron.html#vkCmdPipelineBarrier)`。
- **对抗者必须复查文档引用真实性**: 对抗验证中，对抗者需独立复查审查者引用的每个文档 URL：
  - URL 是否真实存在（不是捏造/幻觉的链接）
  - 被引用的 API 是否真的存在于该文档中
  - API 语义是否与审查者描述一致
  对抗结果中需注明复查过的 URL 及其真实性结论。
- **联网搜索必须用 MCP 工具**: 上述 API 文档验证禁止使用 WebSearch/WebFetch 工具，必须使用 MCP 工具（`web-reader` 的 `webReader`、`web-search-prime` 的 `web_search_prime`）进行搜索和抓取。

## 构建 / 测试 / 调试

### 构建
- 只通过 `python build.py` 构建（见上"禁止手动敲构建命令"）。配置：`--config Debug`（启用 validation 层）或默认 RelWithDebInfo；`--no-configure` 跳过 cmake 配置。一般不手动指定单目标。
- 产物在 `out/build/<preset>/bin/`：Debug → `x64-debug`，RelWithDebInfo → `x64-relWithDebugInfo`。

### 测试（GPUBackendTester）
- `Test/GPUBackendTester`，产物 `bin/GPUBackendTester.exe`。
- CLI：`--backend vulkan|d3d12`、`--test <name>`、`--headless <N>`（N 帧后退出；headless 写 `test_output/*.log` + `result.json`）、`--headless-timeout <N>`、`--report <path>`、`--list`。不带 `--test` 顺序跑全部 7 个测试。
- 退出码 0 = 正常；崩溃写 `crash_YYYYMMDD_HHMMSS.dmp`（当前目录）并在 stderr 打印异常码/模块 base/RVA/调用栈（tester 自带 MiniDump handler）。

### 调试（读 crash dump）
- **标准流程：先读 dump 到函数级根因，再谈处置决策**（不要把半成品根因抛给用户做 scope 决策）。
- 通用工具：`python Tools/read_dump.py <dump.dmp> [--pdb-dir <dir>...]` —— 自动解析异常流/模块表/故障线程 CONTEXT/内存，符号化崩溃地址 + 调用栈（函数 + 源文件:行号）；PDB 搜索路径自动从 dump 内模块路径推导。
- 符号化前置：验证 PDB 与崩溃二进制匹配（在 DLL 内找 `RSDS` 魔数后的 16 字节 GUID，再到 PDB 文件里搜索该字节序列）。
- cdb（Windows SDK Debugging Tools，可选）：`cdb -z <dump.dmp> -y <pdb目录> -c "!analyze -v; q"`。
- 技术细节（minidump 结构、dbghelp 用法、read_dump.py 原理）见 `docs/debugging.md`。
<!-- MANUAL ADDITIONS END -->
