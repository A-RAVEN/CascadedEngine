# 功能规格说明：D3D12RenderBackend AS-IS 基线

**Feature Branch**: as-is/D3D12RenderBackend  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 D3D12RenderBackend 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 D3D12RenderBackend 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 D3D12RenderBackend 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 D3D12RenderBackend。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 D3D12。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 D3D12RenderBackend 的目录层级解释（覆盖 `private/` 中渲染后端主实现与关键子目录）。
- **FR-007**: 系统 MUST 记录 D3D12RenderBackend 的核心模块划分与职责边界（设备初始化、资源管理、图执行、窗口上下文、着色器库接入）。
- **FR-008**: 系统 MUST 记录 D3D12 后端主数据流（模块注入、设备队列初始化、图执行、窗口清理与资源创建）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（DirectX 依赖、CMake/C++标准、模块导出）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=D3D12RenderBackend, type=库, backend=D3D12, status=已启用。
- **RenderBackend_D3D12**: `CRenderBackend` 的 D3D12 实现，封装设备、队列、资源与图执行生命周期。
- **GPUFrameManager**: 帧上下文管理器，为图执行提供帧级资源与同步边界。
- **WindowContext**: 每窗口渲染上下文封装，管理交换链状态与窗口尺寸变化处理。

## 目录结构解释

```text
D3D12RenderBackend/
├── CMakeLists.txt                 # D3D12 后端模块构建与第三方依赖
└── private/
	├── RenderBackend_D3D12.h/.cpp # 后端主类与入口实现
	├── D3D12Includes.h            # D3D12/DXGI 头聚合
	├── D3D12Debug.h               # 调试层与诊断辅助
	├── WindowContext.h/.cpp       # 窗口交换链上下文
	├── ResourceManagment/         # 资源分配、描述符与根签名管理
	├── GPUGraph/                  # 图执行与管线实例化
	├── GPUObjects/                # GPU 对象抽象封装
	├── DescriptorManagment/       # 描述符堆与分配器
	├── ShaderLibrary/             # Shader 导入与运行时库
	├── Utils/                     # 类型与接口翻译工具
	└── Test/                      # 后端测试代码
```

## 核心模块划分

1. 后端主控层
- 入口：`private/RenderBackend_D3D12.h/.cpp`
- 责任：初始化 D3D12 设备、命令队列、模块依赖，调度图执行与窗口生命周期。

2. 资源与描述符管理层
- 入口：`ResourceManagment/`、`DescriptorManagment/`
- 责任：管理 GPU 内存分配、描述符堆、根签名与帧绑定资源。

3. 图执行与管线层
- 入口：`GPUGraph/`
- 责任：将 `GPUGraph` 编译并执行到 D3D12 命令流，组织 raster/compute 管线。

4. 着色器与资源系统接入层
- 入口：`ShaderLibrary/` + `ResourceManagingSystem/ResourceImportingSystem` 依赖
- 责任：加载 shader 库、解析反射并创建 shader struct。

5. 窗口上下文层
- 入口：`WindowContext.h/.cpp`
- 责任：为每个窗口维护交换链上下文与 resize/关闭清理流程。

## 数据流描述

### 数据流 A：后端初始化流程（主流程）

1. `Init(IModuleManager*)` 获取 Timer/IO/Resource/ShaderCompiler 等依赖。
2. 创建 DXGI Factory 并选择硬件适配器。
3. 创建设备与 direct/compute 命令队列。
4. 执行 `AfterDeviceInit` 通知延迟初始化子对象。

### 数据流 B：图执行与窗口维护流程（支撑流程）

1. `ExecuteGraph` 先执行 `CleanupWindowHandles` 处理关闭与 resize。
2. 通过 `GPUFrameManager` 获取帧上下文。
3. `D3D12GPUGraphExecutor` 编译并执行 `GPUGraph`。
4. 按需创建/复用 `WindowContext` 与 GPU 资源对象。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层标准配置）。
- D3D12MemoryAllocator：master（`GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator`）。
- DirectXTex：mar2025（`microsoft/DirectXTex`）。
- DirectX-Headers：1.615.0（`microsoft/DirectX-Headers`）。
- 链接库：`d3d12.lib`、`dxgi`、`dxguid`、`d3dcompiler`。
- 直接依赖：`CACore`、`ThreadManager_Interface`、`Rendering`、`CAGeneralReourceSystem_Interface`、`DirectXTex`、`IOManager`。
- 模块导出：`CA_MODULE_INSTANCE(graphics_backend::CRenderBackend, graphics_backend::RenderBackend_D3D12, RenderBackend_D3D12)`。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
