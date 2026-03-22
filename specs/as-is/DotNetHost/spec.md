# 功能规格说明：DotNetHost AS-IS 基线

**Feature Branch**: as-is/DotNetHost  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 DotNetHost 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 DotNetHost 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 DotNetHost 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 DotNetHost。
- **FR-002**: 系统 MUST 记录目标类型为 可执行程序。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 未启用或可选。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 DotNetHost 的目录层级解释（覆盖 `private/CSharpHost.cpp` 与 CMake 运行时绑定配置）。
- **FR-007**: 系统 MUST 记录 DotNetHost 的核心模块划分与职责边界（hostfxr 加载、函数指针解析、运行时委托获取）。
- **FR-008**: 系统 MUST 记录 .NET Host 启动主数据流（定位 hostfxr、加载导出函数、初始化 runtime context）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（.NET Host 路径版本、Windows 依赖、构建启用状态）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=DotNetHost, type=可执行程序, backend=通用, status=未启用或可选。
- **HostFxrFunctionSet**: `init_fptr/get_delegate_fptr/close_fptr` 三组 hostfxr 核心函数指针。
- **load_hostfxr()**: hostfxr 动态库定位与导出函数绑定流程。
- **get_dotnet_load_assembly(...)**: 获取 `load_assembly_and_get_function_pointer` 委托入口。

## 目录结构解释

```text
DotNetHost/
├── CMakeLists.txt             # DotNetHost 可执行构建与 nethost 导入配置
└── private/
	└── CSharpHost.cpp         # hostfxr/nethost 启动样例实现
```

## 核心模块划分

1. hostfxr 装载层
- 入口：`load_hostfxr()`
- 责任：调用 `get_hostfxr_path` 定位 hostfxr，并动态加载导出函数。

2. 运行时委托获取层
- 入口：`get_dotnet_load_assembly(...)`
- 责任：初始化 runtime config 上下文并获取 assembly 加载函数指针。

3. 可执行入口层
- 入口：`main()`
- 责任：执行 hostfxr 预加载流程，返回成功/失败退出码。

## 数据流描述

### 数据流 A：hostfxr 初始化流程（主流程）

1. 调用 `get_hostfxr_path` 获取 hostfxr 动态库路径。
2. `LoadLibrary` 载入 hostfxr。
3. `GetProcAddress` 解析 `hostfxr_initialize_for_runtime_config` 等导出函数。
4. 保存函数指针用于后续 runtime 初始化。

### 数据流 B：运行时上下文流程（支撑流程）

1. `init_fptr(config_path, ...)` 创建 hostfxr context。
2. `get_delegate_fptr(..., hdt_load_assembly_and_get_function_pointer, ...)` 获取托管入口委托。
3. `close_fptr` 释放 hostfxr context。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（模块显式设置 `CXX_STANDARD 20`）。
- 目标形态：可执行程序（`add_executable(DotNetHost ...)`）。
- .NET Host 依赖：`nethost`（路径固定为 `Microsoft.NETCore.App.Host.win-x64/8.0.4`）。
- 平台依赖：Windows `LoadLibrary/GetProcAddress`。
- 构建状态：顶层 `CMakeLists.txt` 中 `add_subdirectory("DotNetHost")` 当前被注释。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
