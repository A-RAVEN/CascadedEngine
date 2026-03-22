# 功能规格说明：VulkanRendererBackendTester AS-IS 基线

**Feature Branch**: as-is/VulkanRendererBackendTester  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 VulkanRendererBackendTester 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 VulkanRendererBackendTester 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 VulkanRendererBackendTester 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 VulkanRendererBackendTester。
- **FR-002**: 系统 MUST 记录目标类型为 可执行程序。
- **FR-003**: 系统 MUST 记录后端域为 Vulkan。
- **FR-004**: 系统 MUST 记录构建状态为 未启用或可选。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 VulkanRendererBackendTester 的目录层级解释（覆盖 `private/` 中主入口与资源/场景辅助文件）。
- **FR-007**: 系统 MUST 记录 VulkanRendererBackendTester 的核心模块划分与职责边界（模块加载、资源导入、场景渲染、UI 调试工具）。
- **FR-008**: 系统 MUST 记录测试主数据流（模块 loader 初始化、资源扫描、任务图构建、GPU 帧调度）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（可执行依赖、Vulkan 后端接入、构建启用状态）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=VulkanRendererBackendTester, type=可执行程序, backend=Vulkan, status=未启用或可选。
- **TModuleLoaderSet**: 运行时模块加载器集合（ThreadManager/VulkanRenderBackend/WindowSystem/IOManager_FS/CAGeneralReourceSystem）。
- **SceneResourceSet**: 场景运行资源集合（ShaderResrouce、StaticMeshResource、TextureResource、MeshRenderer）。
- **FrameTaskGraphLoop**: `LoopFunction` 内部构建的更新-绘制-提交任务图执行路径。

## 目录结构解释

```text
VulkanRendererBackendTester/
├── CMakeLists.txt                 # Vulkan 测试程序构建
└── private/
	├── Main.cpp                   # 程序主入口与任务图驱动循环
	├── IMGUIContext.*             # 测试器本地 IMGUI 桥接实现
	├── ShaderResource.*           # Shader 资源与导入器
	├── StaticMeshResource.*       # 静态网格资源与导入器
	├── TextureResource.*          # 纹理资源封装
	├── MeshRenderer.*             # 网格渲染辅助
	├── Camera.*                   # 相机控制
	├── JoltTest.cpp               # 物理测试相关代码
	├── MiniDump.*                 # 崩溃转储支持
	└── stb_impl.cpp               # stb_image/stb_image_write 实现
```

## 核心模块划分

1. 模块加载与系统装配层
- 入口：`Main.cpp` 中 `TModuleLoader<...>`
- 责任：动态装配线程、渲染、窗口、资源与 I/O 模块。

2. 资源导入与管理层
- 入口：`ShaderResourceLoaderSlang`、`StaticMeshImporter` + `ResourceManagingSystem`
- 责任：扫描资源目录、导入并按需加载 shader/mesh/texture 资源。

3. 场景与渲染组织层
- 入口：`MeshRenderer/MeshBatcher` 与主循环中图构建逻辑
- 责任：组织场景绘制、灯光参数、最终 blit 和多视口渲染目标。

4. UI 与调试辅助层
- 入口：测试器内 `IMGUIContext`、`MiniDump`
- 责任：提供调试 UI 与异常转储能力。

## 数据流描述

### 数据流 A：启动初始化流程（主流程）

1. 程序创建各模块 loader 并实例化系统组件。
2. 初始化计时系统、线程系统与 IOManager。
3. 初始化资源管理系统，扫描资源目录并导入资产。
4. 初始化 Vulkan 后端并创建主窗口/窗口句柄。

### 数据流 B：帧任务图执行流程（支撑流程）

1. `CThreadManager::LoopFunction` 每帧先更新窗口与 IMGUI。
2. 构建场景 `GPUGraph`（几何、材质、光照、UI）。
3. 组装 `GPUFrame` 并调用 `ScheduleGPUFrame` 提交。
4. ThreadManager 驱动任务执行，直到窗口关闭结束运行。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（模块显式设置 `CXX_STANDARD 20`）。
- 目标形态：可执行程序（`add_executable(VulkanRendererBackendTester ...)`）。
- 直接依赖：`Rendering`、`ShaderCompiler`、`WindowSystem_Interface`、`ThreadManager_Interface`、`CAGeneralReourceSystem_Interface`、`IOManager`、`TimerSystem_Impl`、`CACore`，以及 `glm/IMGUI/assimp/stb/Jolt`。
- 后端接入：运行时加载模块名 `VulkanRenderBackend`。
- 构建状态：顶层 `CMakeLists.txt` 中 `add_subdirectory("VulkanRendererBackendTester")` 当前被注释。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
