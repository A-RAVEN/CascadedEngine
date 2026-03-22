# 功能规格说明：VulkanRenderBackend AS-IS 基线

**Feature Branch**: as-is/VulkanRenderBackend  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 VulkanRenderBackend 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 VulkanRenderBackend 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 VulkanRenderBackend 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 VulkanRenderBackend。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 Vulkan。
- **FR-004**: 系统 MUST 记录构建状态为 未启用或可选。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 VulkanRenderBackend 的目录层级解释（覆盖 `private/` 中应用与图执行相关子目录）。
- **FR-007**: 系统 MUST 记录 VulkanRenderBackend 的核心模块划分与职责边界（后端适配层、Vulkan 应用层、图执行层、对象与资源池）。
- **FR-008**: 系统 MUST 记录 Vulkan 旧后端主数据流（后端初始化、应用调度、图帧提交与资源对象创建）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（VMA 版本、Vulkan SDK 依赖、构建启用状态）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=VulkanRenderBackend, type=库, backend=Vulkan, status=未启用或可选, maturity=未完成（历史旧路径）。
- **CRenderBackend_Vulkan**: 旧版 Vulkan 后端适配类，桥接 `CRenderBackend` 到 `CVulkanApplication`。
- **CVulkanApplication**: Vulkan 应用核心容器，承载窗口、资源、队列与图执行相关子系统。
- **GPUGraphExecutor**: 图执行器，用于把 `GPUGraph` 编译并提交到 Vulkan 命令路径。

## 目录结构解释

```text
VulkanRenderBackend/
├── CMakeLists.txt               # Vulkan 旧后端模块构建（当前未在顶层启用）
└── private/
	├── CRenderBackend_Vulkan.h/.cpp # 旧版后端桥接入口
	├── VulkanApplication.*      # Vulkan 应用核心与子对象管理
	├── GPUGraphExecutor/        # 渲染图执行器
	├── GPUObject*/GPUResources/ # GPU 对象与资源封装
	├── ResourcePool/            # 帧绑定资源池与命令池
	├── ShaderLibrary/           # Shader 库与反射接入
	├── DescriptorAllocation/    # 描述符分配
	├── GPUContexts/             # 帧上下文与执行上下文
	└── WindowContext.*          # 窗口上下文
```

## 核心模块划分

1. 后端桥接层
- 入口：`CRenderBackend_Vulkan.h/.cpp`
- 责任：把统一渲染接口调用转发到 Vulkan 应用对象。

2. Vulkan 应用主控层
- 入口：`VulkanApplication.*`
- 责任：管理实例、设备、窗口句柄、资源系统与图执行协作。

3. 图执行与资源绑定层
- 入口：`GPUGraphExecutor/`、`ShaderStruct/`、`DescriptorAllocation/`
- 责任：把图模型编译为 Vulkan 提交单元并绑定资源。

4. 资源与对象管理层
- 入口：`GPUObject*/GPUResources/`、`ResourcePool/`
- 责任：维护缓冲/纹理对象与帧绑定资源复用。

## 数据流描述

### 数据流 A：初始化流程（主流程）

1. `CRenderBackend_Vulkan::Initialize(...)` 设置全局计时系统。
2. 初始化 `CVulkanApplication` 并接入资源管理系统。
3. 向资源导入系统注册 Vulkan shader 导入器。

### 数据流 B：图调度流程（支撑流程）

1. `ScheduleGPUFrame` 把 `GPUFrame` 提交给 `CVulkanApplication`。
2. 应用层调用图执行器处理渲染图与资源依赖。
3. 资源创建请求通过应用层工厂函数转换为 Vulkan GPU 对象。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层标准配置）。
- VulkanMemoryAllocator：v3.1.0。
- Vulkan SDK 依赖：`VulkanSDK` 目标。
- 直接依赖：`CACore`、`ThreadManager_Interface`、`Rendering`、`ShaderCompiler`、`CAGeneralReourceSystem_Interface`。
- 构建状态：模块目录存在但未在顶层 CMake 启用（当前不参与默认构建）。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
