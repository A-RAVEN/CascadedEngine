# 功能规格说明：VulkanRenderBackendNew AS-IS 基线

**Feature Branch**: as-is/VulkanRenderBackendNew  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 VulkanRenderBackendNew 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 VulkanRenderBackendNew 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 VulkanRenderBackendNew 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 VulkanRenderBackendNew。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 Vulkan。
- **FR-004**: 系统 MUST 记录构建状态为 未启用或可选。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 VulkanRenderBackendNew 的目录层级解释（覆盖后端入口与子系统目录）。
- **FR-007**: 系统 MUST 记录 VulkanRenderBackendNew 的核心模块划分与职责边界（实例设备初始化、队列上下文、布局管理、后端接口占位）。
- **FR-008**: 系统 MUST 记录 Vulkan 新后端主数据流（实例与设备初始化、队列构建、后续执行接口占位现状）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（VMA 版本、VulkanHPP、构建启用状态）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=VulkanRenderBackendNew, type=库, backend=Vulkan, status=未启用或可选, maturity=未完成（在建新路径）。
- **RenderBackend_Vulkan**: 新版 Vulkan 后端实现类，当前已实现实例/设备初始化框架。
- **QueueContext**: 队列创建上下文，负责物理设备队列族信息抽取与创建参数组织。
- **DescriptorSetLayoutContainer / PipelineLayoutContainer**: Vulkan 管线布局相关对象容器。

## 目录结构解释

```text
VulkanRenderBackendNew/
├── CmakeLists.txt                 # Vulkan 新后端模块构建（当前在顶层被注释）
└── private/
	├── RenderBackend_Vulkan.h/.cpp # 后端入口与初始化实现
	├── VulkanQueue/                # 队列上下文
	├── VulkanObjectManaging/       # 描述符与管线布局管理
	├── PipelineStates/             # 管线状态描述
	├── ShaderLibrary/              # Shader 库相关逻辑
	├── Utils/                      # Vulkan 调试与工具封装
	└── VulkanObjectManaging/       # Vulkan 对象管理
```

## 核心模块划分

1. 后端主入口层
- 入口：`private/RenderBackend_Vulkan.h/.cpp`
- 责任：创建 Vulkan instance/device，初始化队列与布局容器。

2. 队列与设备初始化层
- 入口：`VulkanQueue/QueueContext`
- 责任：收集队列族信息并生成 device queue create info。

3. 布局管理层
- 入口：`DescriptorSetLayoutContainer`、`PipelineLayoutContainer`
- 责任：管理 descriptor set layout 与 pipeline layout 生命周期。

4. 渲染接口占位层
- 入口：`ExecuteGraph/CreateGPUBuffer/CreateGPUTexture/...`
- 责任：保留统一后端接口签名，当前多数方法处于待实现状态。

## 数据流描述

### 数据流 A：初始化流程（主流程）

1. `Init(IModuleManager*)` 初始化 Vulkan 动态分发加载器。
2. 创建 instance（含 debug utils 与平台 surface 扩展）。
3. 选择物理设备并创建逻辑设备。
4. 初始化队列上下文与 descriptor layout 容器。

### 数据流 B：执行接口现状（支撑流程）

1. `ExecuteGraph`、资源创建和窗口句柄接口已声明。
2. 当前实现中这些接口仍返回空对象或空行为。
3. 该模块处于“初始化骨架完成、执行路径待补全”阶段。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层标准配置）。
- VulkanMemoryAllocator：v3.1.0。
- Vulkan 头栈：`VulkanHPP`（来自 `$ENV{VK_SDK_PATH}/Include`）。
- 直接依赖：`CACore`、`ThreadManager_Interface`、`Rendering`、`VulkanMemoryAllocator`、`CAGeneralReourceSystem_Interface`、`IOManager`。
- 模块导出：`CA_MODULE_INSTANCE(graphics_backend::CRenderBackend, graphics_backend::RenderBackend_Vulkan, RenderBackend_Vulkan)`。
- 构建状态：`add_subdirectory(VulkanRenderBackendNew)` 在顶层 CMake 当前被注释，默认不构建。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
