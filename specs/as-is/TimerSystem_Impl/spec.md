# 功能规格说明：TimerSystem_Impl AS-IS 基线

**Feature Branch**: as-is/TimerSystem_Impl  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 TimerSystem_Impl 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 TimerSystem_Impl 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 TimerSystem_Impl 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 TimerSystem_Impl。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 TimerSystem_Impl 的目录层级解释（覆盖实现与编辑器文件）。
- **FR-007**: 系统 MUST 记录 TimerSystem_Impl 的核心模块划分与职责边界（事件池、线程局部存储、帧历史、可视化编辑器）。
- **FR-008**: 系统 MUST 记录计时采集主数据流（线程事件入栈/出栈、帧提交、历史查询）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（CMake/C++标准、依赖和模块导出）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=TimerSystem_Impl, type=库, backend=通用, status=已启用。
- **TimerSystem_Impl**: `TimerSystem` 与 `TimerSystem_Editor` 的实现体，负责采集与查询计时数据。
- **ThreadLocalStorage**: 线程局部状态，保存线程 ID、事件栈和当前帧数据缓存。
- **TimerFrameHistories**: 跨线程帧历史容器，维护每个线程最近 N 帧事件记录。

## 目录结构解释

```text
TimerSystem_Impl/
├── CMakeLists.txt                           # TimerSystem_Impl 静态库构建与模块导出配置
├── header/
│   └── TimerSystemEditor/
│       ├── TimerSystem_Impl.h               # 计时编辑器数据结构与查询接口
│       └── TimerSystem_Editor.h             # 编辑器绘制入口声明
└── private/
	├── TimperSystem_Impl.cpp                # 计时系统实现（事件采集、帧历史、全局注册）
	├── TimerSystem_Editor.cpp               # ImGui 时间线可视化实现
	└── IconsFontAwesome4.h                  # 编辑器图标字体定义
```

## 核心模块划分

1. 计时运行时实现层
- 入口：`private/TimperSystem_Impl.cpp`
- 责任：实现 Begin/End/NewFrame，维护线程事件栈和帧历史数据。

2. 事件与线程状态管理层
- 入口：`EventHandlePool`、`ThreadLocalStorage`、`EventStack`
- 责任：保证事件句柄稳定映射，并按线程隔离采集上下文。

3. 帧历史聚合层
- 入口：`TimerFrameHistories`、`FrameCounter`
- 责任：存储并裁剪最近帧数据，支持跨线程历史查询。

4. 编辑器可视化层
- 入口：`private/TimerSystem_Editor.cpp`
- 责任：把计时历史绘制为 ImGui 时间线，支持筛选与悬浮诊断。

## 数据流描述

### 数据流 A：运行时采集流程（主流程）

1. `BeginEvent` 通过 `EventHandlePool` 获取句柄并压入线程事件栈。
2. `EndEvent` 从事件栈弹出匹配事件并写入线程帧缓存。
3. `NewFrame` 推进全局帧计数并触发历史窗口滑动。
4. 线程数据通过 `TimerFrameHistories::SubmitFrame` 进入跨线程历史仓。

### 数据流 B：编辑器查询流程（支撑流程）

1. 编辑器周期性调用 `GetTimerSystemEditor().QueryHistories()`。
2. 查询返回 `TimerData`（起始帧、帧时间点、线程帧事件集合）。
3. UI 层据此绘制分线程时间线与事件条目。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层标准配置）。
- 目标形态：STATIC（`add_library(TimerSystem_Impl STATIC ...)`）。
- 直接依赖：`CACore`、`TimerSystem`、`IMGUI`。
- 模块导出：`CA_MODULE_INSTANCE(catimer::TimerSystem, catimer::TimerSystem_Impl, CATimer)`。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
