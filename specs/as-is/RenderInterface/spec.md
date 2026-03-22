# 功能规格说明：RenderInterface AS-IS 基线

**Feature Branch**: as-is/RenderInterface  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 RenderInterface 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 RenderInterface 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 RenderInterface 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 RenderInterface。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 RenderInterface 的目录层级解释（至少覆盖 `header/` 与 `private/` 关键文件）。
- **FR-007**: 系统 MUST 记录 RenderInterface 的核心模块划分与职责边界（后端抽象、图调度、资源句柄、命令抽象、线程上下文）。
- **FR-008**: 系统 MUST 记录 GPU 帧构建主数据流（资源声明、数据上传、Pass 编排、最终呈现）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（CMake/C++标准与直接依赖模块）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=RenderInterface, type=库, backend=通用, status=已启用。
- **GPUFrame**: 一帧渲染提交单元，包含 `GPUGraph m_RenderGraph` 与 `vector<ImageHandle> m_PresentWindows`。
- **GPUGraph**: 图调度容器，维护渲染/计算/数据传输/Finalize 阶段序列及其索引映射。
- **ResourceHandleKeyData**: 资源唯一键结构（`name + uniqueID + threadID`），用于区分共享与线程局部资源。

## 目录结构解释

```text
RenderInterface/
├── CMakeLists.txt             # RenderInterface 构建定义（目标 Rendering）
├── header/
│   ├── RenderInterfaceManager.h # 渲染接口模块管理入口（当前为轻量占位）
│   ├── CRenderBackend.h         # 跨后端核心接口：图执行、资源创建、窗口与运行控制
│   ├── GPUGraph.h               # 渲染图数据模型：Pass/Dispatch/上传/Finalize/子图
│   ├── GPUFrame.h               # 帧级封装：图 + 待呈现窗口集合
│   ├── ShaderResourceHandle.h   # 图内资源句柄（ImageHandle/BufferHandle）与唯一键
│   ├── CCommandList.h           # 命令列表抽象（draw/drawIndexed/scissor）
│   ├── GPUTexture.h             # 纹理描述与视图定义
│   ├── GPUBuffer.h              # 缓冲描述定义
│   ├── ShaderProvider.h         # 着色器集合与反射访问接口
│   └── Common.h                 # 渲染枚举与通用类型（格式/访问/着色阶段等）
└── private/
	├── ThreadData.cpp           # 线程全局/局部 ID 分配与查询
	├── pch.h                    # 预编译头
	└── pch.cpp                  # 预编译头编译单元
```

## 核心模块划分

1. 后端统一抽象层
- 入口：`header/CRenderBackend.h`
- 责任：定义与图形 API 无关的执行入口（`ExecuteGraph`）、资源工厂（`CreateImage/CreateBuffer`）、窗口接入与生命周期控制（`Init/Run/Shutdown`）。

2. 渲染图与帧编排层
- 入口：`header/GPUGraph.h`、`header/GPUFrame.h`
- 责任：通过 `RenderPass/ComputeBatch/GPUDataTransfers/FinalizePass` 建模一帧工作负载，并记录阶段顺序与资源依赖。

3. 资源句柄与描述层
- 入口：`header/ShaderResourceHandle.h`、`header/GPUTexture.h`、`header/GPUBuffer.h`
- 责任：统一内部/外部资源标识，承载图内分配描述，支持共享与线程局部资源命名策略。

4. 命令与着色器接入层
- 入口：`header/CCommandList.h`、`header/ShaderProvider.h`
- 责任：定义绘制命令抽象与着色器源码/反射信息访问协议，供后端实现侧消费。

5. 线程上下文支撑层
- 入口：`private/ThreadData.cpp`
- 责任：提供全局线程 ID 与线程局部 ID，参与资源唯一键生成，避免多线程资源命名冲突。

## 数据流描述

### 数据流 A：GPU 帧构建与执行（主流程）

1. 上层构建 `GPUFrame`，并初始化 `m_RenderGraph`。
2. 通过 `GPUGraph` 依次声明阶段：
   - `AllocImage/AllocBuffer` 注册图内资源描述；
   - `ScheduleData` 写入上传任务；
   - `AddPass(Rast/Comp)` 添加渲染或计算阶段；
   - `Finalize/Present` 声明末端资源状态与回显窗口。
3. `GPUFrame::m_PresentWindows` 记录本帧需要交换链呈现的窗口句柄。
4. 后端实现接收 `CRenderBackend::ExecuteGraph(graphs)`，按阶段序列执行并完成呈现。

### 数据流 B：资源键与图内描述索引（支撑流程）

1. 资源通过 `ImageHandle/BufferHandle` 生成 `ResourceHandleKeyData`（共享或线程局部）。
2. `GraphResourceManager` 在图构建期执行 `RegisterHandle`，建立 `handle -> descriptor index` 映射。
3. 执行前通过 `GetDescriptorIndex/GetDescriptor` 查询描述，驱动后端资源创建与绑定。
4. 无效句柄或缺失映射时触发日志/断言，阻断错误资源访问路径。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（`RenderInterface/CMakeLists.txt` 中 `set(CMAKE_CXX_STANDARD 20)`）。
- 预编译头：启用（`target_precompile_headers(Rendering PUBLIC private/pch.h)`）。
- 直接依赖模块：`WindowSystem_Interface`、`ShaderCompiler`、`TimerSystem`、`IOManager`、`CACore`、`ThreadManager_Interface`。
- 渲染图能力域：图形 Pass + 计算 Pass + 数据上传 + Finalize/Present（由 `GPUGraph` 数据结构提供）。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
