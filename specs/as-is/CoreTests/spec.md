# 功能规格说明：CoreTests AS-IS 基线

**Feature Branch**: as-is/CoreTests  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 CoreTests 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 CoreTests 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 CoreTests 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 CoreTests。
- **FR-002**: 系统 MUST 记录目标类型为 可执行程序。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 CoreTests 的目录层级解释（覆盖 `SerializerTest.cpp` 的测试入口）。
- **FR-007**: 系统 MUST 记录 CoreTests 的核心模块划分与职责边界（序列化验证、哈希验证、分配器行为验证）。
- **FR-008**: 系统 MUST 记录测试执行主数据流（构造测试数据、执行序列化/反序列化、对比与统计输出）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（CMake/C++标准、可执行目标与依赖）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=CoreTests, type=可执行程序, backend=通用, status=已启用。
- **SerializerTestMain**: `main()` 测试入口，当前默认执行 `TestAllocation()`。
- **TestStructFamily**: `TestTruct0/TestStruct1/TestStruct2/TestStruct3` 等用于反射、序列化与哈希验证的样例结构体。
- **HashAndSerializerPipeline**: `cacore::serialize/deserialize/hash` 组合验证路径。

## 目录结构解释

```text
CoreTests/
├── CMakeLists.txt              # CoreTests 可执行目标构建
└── SerializerTest.cpp          # 核心序列化/哈希/分配测试代码
```

## 核心模块划分

1. 序列化验证层
- 入口：`TestSerialize0`、`TestHash*`
- 责任：验证聚合类型、容器类型和自定义反射类型的序列化/反序列化一致性。

2. 哈希一致性验证层
- 入口：`TestHash`、`TestHash1`、`TestHash2`
- 责任：验证 `cacore::HashObj`、自定义 `ca_hash` 与容器哈希行为。

3. 内存分配行为验证层
- 入口：`TestAllocation`
- 责任：验证对象/数组/new-delete 与容器分配路径，输出 mimalloc 统计信息。

## 数据流描述

### 数据流 A：结构体序列化验证（主流程）

1. 构造测试结构体与容器数据。
2. 调用 `cacore::serialize` 写入字节缓冲。
3. 调用 `cacore::deserialize` 或反序列化器还原对象。
4. 通过比较/哈希映射检查恢复结果与预期一致性。

### 数据流 B：分配器验证流程（支撑流程）

1. 分别执行数组分配、单对象分配、容器分配。
2. 完成释放后调用 `mi_stats_print` 输出分配统计。
3. 用于观察全局分配器接入是否按预期工作。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（模块显式设置 `CXX_STANDARD 20`）。
- 目标形态：可执行程序（`add_executable(CoreTests ...)`）。
- 直接依赖：`CACore`、`glm`。
- 分配器观测：`mimalloc` 统计接口（`mi_stats_print`）。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
