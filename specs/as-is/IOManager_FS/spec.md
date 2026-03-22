# 功能规格说明：IOManager_FS AS-IS 基线

**Feature Branch**: as-is/IOManager_FS  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 IOManager_FS 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 IOManager_FS 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 IOManager_FS 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 IOManager_FS。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 IOManager_FS 的目录层级解释（覆盖模块构建与 `IOManager.cpp` 主实现）。
- **FR-007**: 系统 MUST 记录 IOManager_FS 的核心模块划分与职责边界（批处理缓存、读写批实现、流缓存与模块导出）。
- **FR-008**: 系统 MUST 记录文件批处理主数据流（请求聚合、区间合并、线程调度执行、结果回填/落盘）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（CMake/C++标准、模块依赖与并发实现方式）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=IOManager_FS, type=库, backend=通用, status=已启用。
- **BatchCache**: 批处理缓存，维护文件偏移游标、区间块列表与可合并逻辑。
- **IOBatchImpl / WriteBatchImpl**: 读/写批处理实现，基于 `TaskScheduler` 异步执行并在 `SubmitAndWait` 同步完成。
- **IOManagerImpl**: `IOManager` 实现体，负责线程管理器接入、批对象创建与输出流缓存管理。

## 目录结构解释

```text
IOManager_FS/
├── CMakeLists.txt             # IOManager_FS 模块构建与依赖声明
└── IOManager.cpp              # 文件系统 I/O 批处理实现与模块导出
```

## 核心模块划分

1. 读写区间聚合层
- 入口：`IOManager.cpp` 中 `IORead`、`IORange`、`IOChunkState`、`BatchCache`
- 责任：将连续或重叠请求合并为块，减少文件 seek/read/write 次数。

2. 批处理执行层
- 入口：`IOBatchImpl`、`WriteBatchImpl`
- 责任：维护请求生命周期，提交任务到线程调度器并等待完成。

3. 管理与流缓存层
- 入口：`IOManagerImpl`、`OStreamContext`、`OStreamLock`
- 责任：注入 `CThreadManager`、复用文件输出流、保护并发写入互斥。

4. 模块导出层
- 入口：文件尾 `CA_MODULE_INSTANCE(ca_io::IOManager, ca_io::IOManagerImpl, IOManager_FS)`
- 责任：将文件系统实现注册为 `IOManager` 运行时模块实例。

## 数据流描述

### 数据流 A：批量读取流程（主流程）

1. 调用 `IOManager::Batch(path)` 获取 `IOBatchImpl`。
2. 多次 `Read/Seek` 被记录到 `BatchCache` 并执行区间合并。
3. `SubmitAndWait` 创建调度器任务，在任务中按 chunk 读取文件到 stageBuffer。
4. 按原始子请求偏移把 stageBuffer 拆分拷贝回调用方目标地址。
5. 清空缓存并增加提交计数。

### 数据流 B：批量写入流程（支撑流程）

1. 调用 `IOManager::WriteBatch(path)` 获取 `WriteBatchImpl`。
2. 多次 `Write/Seek` 聚合为 chunk 列表。
3. `SubmitAndWait` 从 `IOManagerImpl` 取得带锁输出流，避免并发写冲突。
4. 先把子写请求聚合到 stageBuffer，再按 chunk 一次性写入并 flush。
5. 清空缓存并增加提交计数。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层标准配置）。
- 目标形态：MODULE（`add_library(IOManager_FS MODULE ...)`）。
- 直接依赖：`CACore`、`IOManager`。
- 并发调度：通过 `ThreadManager` 的 `TaskScheduler` 执行批处理任务。
- 文件能力：`std::filesystem`、`std::ifstream`、`std::ofstream`。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
