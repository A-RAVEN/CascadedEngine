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
- **禁止 sleep 轮询异步任务**: Workflow/Agent/Bash 异步任务完成后会自动发送通知，禁止用 `sleep N` 循环轮询等待
- **审查闭环**: tasks 中最后一个任务必须是审查任务（Review & Adversarial Verify）。对刚完成的所有任务做正确性/完整性/诚实性审查，需开 workflow 做对抗验证。验证发现的新问题必须添加为新的 task（附加 `[AUDIT]` 前缀），然后追加一个新的审查任务，继续执行。循环直到审查无新问题或达到 3 轮。审查结果写入 tasks.md 末尾的 `## Review Log` 区域。**重要：即使达到 3 轮上限停止，停下的那一刻交付的必须是审查结果报告，而不是"完成了第 N 个任务"之类的实现进度汇报。用户看到的最后一个输出应该是 Review Log。**
<!-- MANUAL ADDITIONS END -->
