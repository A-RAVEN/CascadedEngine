# 功能规格说明：ThreadManager AS-IS 基线

**Feature Branch**: as-is/ThreadManager  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 ThreadManager 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 ThreadManager 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 ThreadManager 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 ThreadManager。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 ThreadManager 的目录层级解释（覆盖 `header/` 与 `private/` 中任务调度关键文件）。
- **FR-007**: 系统 MUST 记录 ThreadManager 的核心模块划分与职责边界（任务接口、节点实现、队列/Worker、事件管理）。
- **FR-008**: 系统 MUST 记录任务调度主数据流（任务创建、依赖收敛、入队执行、完成回调与回收）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（CMake/C++标准、模块形态与依赖）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=ThreadManager, type=库, backend=通用, status=已启用。
- **CThreadManager**: 线程系统主接口，负责线程池初始化、任务队列映射、调度器创建与运行循环。
- **TaskNode**: 调度执行最小节点，包含依赖/后继关系、事件等待与信号字段、运行状态。
- **TaskScheduler_Impl**: 调度器实现，负责提交任务图、等待执行完成及子任务生命周期管理。

## 目录结构解释

```text
ThreadManager/
├── CMakeLists.txt               # ThreadManager 模块与 Interface 目标构建
├── header/
│   └── ThreadManager.h          # 对外任务系统接口（Task/Scheduler/ThreadManager）
└── private/
	├── ThreadManager_Impl.h     # 内部实现声明（队列/Worker/事件管理）
	├── ThreadManager_Impl.cpp   # 调度主流程、Worker 循环、事件联动实现
	├── TaskNode.h               # 任务节点基类与状态机定义
	├── TaskNode.cpp             # 依赖计数、完成通知、回收路径
	├── pch.h                    # 预编译头
	├── pch.cpp                  # 预编译头编译单元
	├── framework.h              # Windows 框架头
	└── dllmain.cpp              # 模块入口
```

## 核心模块划分

1. 对外任务抽象层
- 入口：`header/ThreadManager.h`
- 责任：定义 `CTask/TaskParallelFor/CTaskGraph/TaskScheduler/CThreadManager` 接口与链式配置协议。

2. 节点状态与依赖管理层
- 入口：`private/TaskNode.h`、`private/TaskNode.cpp`
- 责任：维护依赖计数、后继传播、事件等待/触发信息与执行后清理。

3. 调度器与任务池层
- 入口：`TaskScheduler_Impl`、`TaskNodeAllocator`
- 责任：批量提交节点、根据依赖入队、等待完成，并通过线程安全对象池复用节点。

4. 队列/Worker 执行层
- 入口：`SharedTaskQueue`、`GeneralTaskWorker`
- 责任：管理队列分发与线程工作循环，执行节点并触发回收。

5. 事件同步层
- 入口：`TaskNodeEventManager`
- 责任：维护按事件名与帧序号组织的等待列表，`SignalEvent` 后批量唤醒可执行节点。

## 数据流描述

### 数据流 A：任务提交与执行（主流程）

1. 上层通过 `CThreadManager::NewScheduler` 创建调度器。
2. 调度器创建节点（Task/ParallelFor/TaskGraph），配置依赖与事件关系。
3. `TaskScheduler_Impl::Execute` 统计可运行节点并初始化依赖计数。
4. 无依赖节点入队，Worker 在 `WorkLoop/InlineWorkLoop` 中取出并执行。
5. 节点执行完成后调用 `FinalizeExecution_Internal`：通知后继、触发事件、通知 owner。
6. 节点经 `TaskNodeAllocator` 回收，调度器等待计数归零后结束。

### 数据流 B：事件门控执行（支撑流程）

1. 节点声明 `WaitOnEvent(event)` 或 `SignalEvent(event)`。
2. `TaskNodeEventManager::WaitEventDone` 将未满足帧条件的节点挂入等待队列。
3. `ThreadManager_Impl::SignalEvent` 更新事件已触发帧。
4. 满足帧条件的等待节点被重新入队，继续执行流程。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（模块与 Interface 目标均设置 `CXX_STANDARD 20`）。
- 目标形态：`ThreadManager` 为 MODULE；`ThreadManager_Interface` 为 INTERFACE。
- 预编译头：启用（`private/pch.h`）。
- 直接依赖：`CACore`、`TimerSystem`（Interface 同步暴露 `TimerSystem` 与 `CACore`）。
- 平台特性：Windows 线程命名与线程描述接口（`SetThreadDescription`）。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
