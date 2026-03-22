# 功能规格说明：ShaderCompilerSlang AS-IS 基线

**Feature Branch**: as-is/ShaderCompilerSlang  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 ShaderCompilerSlang 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 ShaderCompilerSlang 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 ShaderCompilerSlang 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 ShaderCompilerSlang。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 ShaderCompilerSlang 的目录层级解释（覆盖 `private/Compiler.cpp` 与模块导出路径）。
- **FR-007**: 系统 MUST 记录 ShaderCompilerSlang 的核心模块划分与职责边界（Slang 会话、编译流程、反射提取、编译器池）。
- **FR-008**: 系统 MUST 记录 Slang 编译主数据流（任务配置、模块装载、目标编译、反射构建与结果汇总）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（Slang 版本、CMake/C++标准与直接依赖）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=ShaderCompilerSlang, type=库, backend=通用, status=已启用。
- **Compiler_Impl**: `IShaderCompiler` 的 Slang 实现，封装 `IGlobalSession/ISession` 与 `DoCompile` 逻辑。
- **ShaderCompilerManager**: 编译器实例池管理器，提供阻塞式获取与归还（条件变量 + 对象池向量）。
- **ShaderCompileTargetResult**: 每个目标格式输出（SPIR-V/DXIL/HLSL）及对应反射数据。

## 目录结构解释

```text
ShaderCompilerSlang/
├── CMakeLists.txt             # Slang 模块构建、第三方下载与 DLL 复制
└── private/
	├── Compiler.cpp           # Slang 编译器实现（编译 + 反射 + 编译器池）
	└── TestPrint.h            # Slang 反射调试打印辅助（示例/实验性质）
```

## 核心模块划分

1. Slang 会话与编译任务管理层
- 入口：`private/Compiler.cpp` 中 `Compiler_Impl`
- 责任：管理全局会话、每次编译任务会话参数、目标 profile、宏与搜索路径。

2. 目标编译与程序产物层
- 入口：`Compiler_Impl::DoCompile`
- 责任：加载模块、聚合入口点、链接程序、按目标导出内核字节码并标注 shader stage。

3. 反射解析与绑定建模层
- 入口：`ReflectBindings`、`ReflectTypeLayouts`、`ReflectVertexAttributes`
- 责任：提取资源绑定空间、struct/uniform 布局、顶点输入语义和资源 usage mask。

4. 编译器实例池层
- 入口：`ShaderCompilerManager`
- 责任：维护固定数量编译器实例，提供线程安全的获取/归还流程。

## 数据流描述

### 数据流 A：Slang 编译流程（主流程）

1. `ShaderCompilerManager` 分配 `Compiler_Impl`。
2. 编译器收集 target/searchPath/macro/moduleName 并构建 `SessionDesc`。
3. `DoCompile` 创建 `ISession`，装载模块，拼接入口点后链接为 `linkedProgram`。
4. 按 target 索引导出 entry point 代码与阶段类型，生成 `ShaderProgramData`。
5. 聚合为 `ShaderCompileTargetResult` 列表返回上层。

### 数据流 B：反射构建流程（支撑流程）

1. 通过 `ProgramLayout` 获取全局参数与入口参数布局。
2. `ReflectRootTypeLayouts/ReflectTypeLayouts` 提取 struct、uniform、纹理与缓冲资源结构。
3. `ReflectBindings` 建立 binding hierarchy 与各 space 的资源统计。
4. 顶点入口额外执行 `ReflectVertexAttributes`，补齐输入语义/位置。
5. 反射结果写入 `ShaderCompileTargetResult.m_ReflectionData`。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层标准配置）。
- Slang：2025.10（`https://github.com/shader-slang/slang/releases/download/v2025.10/...`）。
- 目标形态：MODULE（`add_library(ShaderCompilerSlang MODULE ...)`）。
- 直接依赖：`ShaderCompiler`、`CACore`、`Rendering`、`slang::slang`。
- 模块导出：`CA_MODULE_INSTANCE(IShaderCompilerManager, ShaderCompilerManager, ShaderCompilerManager_Slang)`。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
