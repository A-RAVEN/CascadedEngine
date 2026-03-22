# 功能规格说明：TimerSystem AS-IS 基线

**Feature Branch**: as-is/TimerSystem  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 TimerSystem 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 TimerSystem 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 TimerSystem 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 TimerSystem。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 TimerSystem 的目录层级解释（覆盖 `header/CATimer/Timer.h` 与 `private/Timer.cpp`）。
- **FR-007**: 系统 MUST 记录 TimerSystem 的核心模块划分与职责边界（计时接口、全局注册、作用域埋点宏）。
- **FR-008**: 系统 MUST 记录 CPU 埋点主数据流（作用域进入、事件开始、作用域析构、事件结束、帧推进）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（CMake/C++标准、库形态与依赖）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=TimerSystem, type=库, backend=通用, status=已启用。
- **TimerSystem**: 计时接口抽象，定义 `SetThreadName/BeginEvent/EndEvent/NewFrame`。
- **CPUTimerScope**: RAII 作用域对象，在构造和析构阶段自动调用 `BeginEvent/EndEvent`。
- **GlobalTimerPtr**: 由 `SetGlobalTimerSystem/GetGlobalTimerSystem` 维护的全局计时系统指针。

## 目录结构解释

```text
TimerSystem/
├── CMakeLists.txt             # TimerSystem 静态库构建
├── header/
│   └── CATimer/
│       └── Timer.h            # 计时接口、全局访问函数、CPUTIMER_SCOPE 宏
└── private/
	└── Timer.cpp              # 全局 TimerSystem 指针定义与访问实现
```

## 核心模块划分

1. 计时接口层
- 入口：`header/CATimer/Timer.h`
- 责任：定义统一计时协议，向各模块暴露事件埋点入口。

2. 全局计时注册层
- 入口：`private/Timer.cpp`
- 责任：维护进程级计时系统实例指针，支持跨模块共享计时后端。

3. 作用域埋点层
- 入口：`CPUTimerScope` 与 `CPUTIMER_SCOPE/TIMER_NEWFRAME` 宏
- 责任：提供低侵入度埋点方式，自动配对 Begin/End 并推进帧计数。

## 数据流描述

### 数据流 A：作用域埋点流程（主流程）

1. 代码进入 `CPUTIMER_SCOPE(...)` 作用域。
2. `CPUTimerScope` 构造调用 `GetGlobalTimerSystem()->BeginEvent(...)`。
3. 作用域结束时析构函数触发 `EndEvent(...)`。
4. 系统通过 `TIMER_NEWFRAME()` 在帧边界调用 `NewFrame()`。

### 数据流 B：计时后端接入流程（支撑流程）

1. 具体计时实现模块调用 `SetGlobalTimerSystem(impl)` 注册全局实例。
2. 业务代码通过 `GetGlobalTimerSystem()` 间接访问实现。
3. 使计时接口与具体后端解耦，支持替换不同实现模块。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层标准配置）。
- 目标形态：STATIC（`add_library(TimerSystem STATIC ...)`）。
- 直接依赖：`CACore`。
- 计时语义：基于 RAII 的作用域事件采集约定。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
