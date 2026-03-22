# 功能规格说明：ShaderCompiler AS-IS 基线

**Feature Branch**: as-is/ShaderCompiler  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 ShaderCompiler 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 ShaderCompiler 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 ShaderCompiler 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 ShaderCompiler。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 ShaderCompiler 的目录层级解释（覆盖 `CMakeLists.txt` 与 `header/Compiler.h`）。
- **FR-007**: 系统 MUST 记录 ShaderCompiler 的核心模块划分与职责边界（编译协议、反射数据模型、编译器池管理协议）。
- **FR-008**: 系统 MUST 记录着色器编译接口主数据流（任务配置、目标编译、结果回收与反射消费）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（CMake/C++标准与直接依赖）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=ShaderCompiler, type=库, backend=通用, status=已启用。
- **IShaderCompiler**: 抽象编译器协议，定义 `BeginCompileTask/SetTarget/AddSourceFile/Compile/GetResults` 生命周期。
- **ShaderCompileTargetResult**: 单目标编译结果，包含 `targetType`、`shaderTypeFlags`、`programs` 与 `m_ReflectionData`。
- **ShaderReflectionData**: 反射结构体，包含 binding space 数据、顶点输入属性、结构体元数据与层级绑定信息。

## 目录结构解释

```text
ShaderCompiler/
├── CMakeLists.txt            # 头文件接口库定义（INTERFACE）
└── header/
	└── Compiler.h            # 着色器编译与反射的公共类型/接口协议
```

## 核心模块划分

1. 编译任务协议层
- 入口：`header/Compiler.h` 中 `IShaderCompiler`
- 责任：定义编译任务配置（目标/宏/源文件/入口点）与执行、错误检查、结果导出协议。

2. 反射数据模型层
- 入口：`header/Compiler.h` 中 `ShaderReflectionData`、`ShaderBindingInfo`、`ShaderStructData`
- 责任：定义统一的资源绑定、uniform 布局、结构体层级与顶点输入描述，供后端消费。

3. 编译器管理协议层
- 入口：`header/Compiler.h` 中 `IShaderCompilerManager`
- 责任：定义编译器实例池的获取/归还与容量初始化协议，支撑并发编译请求管理。

## 数据流描述

### 数据流 A：编译请求与结果输出（主流程）

1. 业务侧通过 `IShaderCompilerManager::AquireShaderCompiler` 获取编译器实例。
2. 调用 `BeginCompileTask`，设置 include path/target/macro，追加源文件与入口信息。
3. 调用 `Compile` 执行编译。
4. 通过 `HasError/GetResults` 回收诊断与目标结果。
5. 调用 `EndCompileTask` 清理任务上下文并归还编译器实例。

### 数据流 B：反射信息消费（支撑流程）

1. 每个 `ShaderCompileTargetResult` 输出 `programs` 与 `m_ReflectionData`。
2. `m_ReflectionData` 中的 binding space、资源层级与结构体元数据被渲染后端用于资源布局构建。
3. `shaderTypeFlags` 用于渲染管线按阶段选择可用入口程序。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层 `set(CMAKE_CXX_STANDARD 20)`）。
- 目标形态：INTERFACE 头文件库（`add_library(ShaderCompiler INTERFACE ...)`）。
- 直接依赖：`CACore`（用于 NameHash、容器封装与基础工具）。
- 类型耦合：编译与资源语义枚举依赖 `RenderInterface` 公共类型（`Common.h`）。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
