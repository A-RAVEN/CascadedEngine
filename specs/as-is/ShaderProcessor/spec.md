# 功能规格说明：ShaderProcessor AS-IS 基线

**Feature Branch**: as-is/ShaderProcessor  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 ShaderProcessor 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 ShaderProcessor 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 ShaderProcessor 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目定位为历史遗留工具，当前主线流程无直接依赖，属于无用旧项目（待下线或归档候选）。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 ShaderProcessor。
- **FR-002**: 系统 MUST 记录目标类型为 可执行程序。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 未启用或可选。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 ShaderProcessor 的目录层级解释（覆盖 `Main.cpp` 与 DXC 构建链接配置）。
- **FR-007**: 系统 MUST 记录 ShaderProcessor 的核心模块划分与职责边界（源文件加载、DXC 编译任务配置、诊断输出）。
- **FR-008**: 系统 MUST 记录着色器处理主数据流（读取源码、构造编译参数、执行编译、收集输出/错误）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（DXC 接入、CMake/C++标准、构建启用状态）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=ShaderProcessor, type=可执行程序, backend=通用, status=未启用或可选, maturity=未完成/历史遗留, recommendation=不建议新增投入。
- **DxcCompilerPipeline**: `IDxcUtils + IDxcCompiler3 + IDxcResult` 构成的编译执行流水线。
- **ShaderCompileArgs**: 目标参数集合（`-spirv`、`-fspv-target-env=vulkan1.3`、`-E frag`、`-T ps_6_6` 等）。
- **CompileDiagnostics**: DXC 错误与预处理输出读取路径（`DXC_OUT_ERRORS/DXC_OUT_HLSL`）。

## 目录结构解释

```text
ShaderProcessor/
├── CMakeLists.txt             # ShaderProcessor 可执行构建与 DXC 链接
└── Main.cpp                   # HLSL -> DXC 编译处理样例入口
```

## 核心模块划分

1. 源码输入层
- 入口：`Main.cpp` 中 `fileloading_utils::LoadStringFile`
- 责任：加载待编译 HLSL 源文本并转换为 DXC 输入 blob。

2. DXC 编译配置层
- 入口：`arguments` 向量构建逻辑
- 责任：配置预处理、目标后端、入口点、profile 与诊断选项。

3. 编译执行与诊断层
- 入口：`IDxcCompiler3::Compile` + `IDxcResult::GetOutput`
- 责任：执行编译并输出错误信息或预处理结果。

## 数据流描述

### 数据流 A：编译执行流程（主流程）

1. 读取 shader 源文件字符串。
2. 创建 `IDxcUtils/IDxcCompiler3` 与源 blob。
3. 构造 `DxcBuffer` 与编译参数列表。
4. 调用 `Compile` 获取编译结果对象。
5. 读取错误输出；若无错误则读取预处理输出并打印。

### 数据流 B：参数驱动目标流程（支撑流程）

1. 设置 `-spirv` 与 Vulkan 目标环境。
2. 指定入口点 `frag` 与 profile `ps_6_6`。
3. 开启 `-WX` 与 `-Zi` 用于严格编译与调试信息。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（模块显式设置 `CXX_STANDARD 20`）。
- 目标形态：可执行程序（`add_executable(ShaderProcessor ...)`）。
- DXC 接入：通过 `CMake/ExternalProjects.cmake` 中 `link_dxc(...)`。
- 直接依赖：`CACore` 文件加载与日志能力。
- 构建状态：顶层 CMake 未加入 `add_subdirectory("ShaderProcessor")`（当前默认不构建）。
- 生命周期建议：作为历史遗留工具保留基线记录，优先评估归档/删除，而非继续扩展功能。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
