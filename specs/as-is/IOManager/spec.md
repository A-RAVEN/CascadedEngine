# 功能规格说明：IOManager AS-IS 基线

**Feature Branch**: as-is/IOManager  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 IOManager 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 IOManager 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 IOManager 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 IOManager。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 IOManager 的目录层级解释（覆盖接口头与空实现现状）。
- **FR-007**: 系统 MUST 记录 IOManager 的核心模块划分与职责边界（批处理读写接口、管理接口、实现外置策略）。
- **FR-008**: 系统 MUST 记录 I/O 接口主数据流（批任务创建、读写请求累积、提交执行契约）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（CMake/C++标准、目标形态与依赖）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=IOManager, type=库, backend=通用, status=已启用。
- **IOBatch**: 读批处理抽象，定义 `Read/Seek/SubmitAndWait/SubmitCount`。
- **WBatch**: 写批处理抽象，定义 `Write/Seek/SubmitAndWait/SubmitCount`。
- **IOManager**: I/O 管理接口，定义批处理对象工厂（`Batch`、`WriteBatch`）与初始化入口。

## 目录结构解释

```text
IOManager/
├── CMakeLists.txt                 # IOManager 静态库构建
├── header/
│   └── IOManager/
│       └── IOManager.h            # I/O 抽象接口定义（读批、写批、管理器）
└── private/
	└── IOManager.cpp              # 当前为空实现（接口库占位）
```

## 核心模块划分

1. 读批处理接口层
- 入口：`header/IOManager/IOManager.h` 中 `IOBatch`
- 责任：提供顺序读请求累计、偏移控制与提交同步等待协议。

2. 写批处理接口层
- 入口：`header/IOManager/IOManager.h` 中 `WBatch`
- 责任：提供顺序写请求累计、偏移控制与提交同步等待协议。

3. I/O 管理抽象层
- 入口：`header/IOManager/IOManager.h` 中 `IOManager`
- 责任：对外暴露批处理实例创建与线程管理器注入接口。

4. 实现解耦边界
- 入口：`private/IOManager.cpp`（当前为空）
- 责任：声明本模块仅提供抽象契约，具体文件系统实现由下游模块（如 `IOManager_FS`）提供。

## 数据流描述

### 数据流 A：接口调用流程（主流程）

1. 业务通过模块系统获取 `IOManager` 接口实例。
2. 调用 `Batch(filePath)` 或 `WriteBatch(filePath)` 获取批处理对象。
3. 批处理对象累积 `Read/Write` 与 `Seek` 请求。
4. 调用 `SubmitAndWait` 提交并等待执行完成。
5. 通过 `SubmitCount` 观测提交次数。

### 数据流 B：实现委托边界（支撑流程）

1. IOManager 本体仅定义协议，不承担实际文件访问。
2. 运行时由实现模块（当前为 `IOManager_FS`）承接真实读写与并发调度。
3. 这样使上层逻辑仅依赖稳定接口，降低后端实现替换成本。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层标准配置）。
- 目标形态：STATIC（`add_library(IOManager STATIC ...)`）。
- 直接依赖：`CACore`、`ThreadManager_Interface`。
- 当前实现状态：接口头完整、私有实现文件为空，属于“契约先行”架构。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
