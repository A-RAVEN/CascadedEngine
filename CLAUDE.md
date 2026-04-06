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

# Add commands for C++ (CMake build system)

## Code Style

C++ (CMake build system): Follow standard conventions

## Recent Changes
- 002-vulkan-backend-renderinterface: Added C++20 (CMake 3.12+, MSVC toolchain)

- 001-reorganize-subprojects: Added C++ (CMake build system) + CMake 3.x, vcpkg (for external dependencies)

<!-- MANUAL ADDITIONS START -->
## 工作规则

- **禁止编译/配置**: 除非用户明确说明，不要尝试配置或编译此项目
- **禁止自动执行后续任务**: 除非用户明确说明继续实行任务，禁止执行tasks中后续的任务
<!-- MANUAL ADDITIONS END -->
