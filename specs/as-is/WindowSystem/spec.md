# 功能规格说明：WindowSystem AS-IS 基线

**Feature Branch**: as-is/WindowSystem  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 WindowSystem 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 WindowSystem 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 WindowSystem 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 WindowSystem。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 WindowSystem 的目录层级解释（覆盖接口头与 GLFW 实现文件）。
- **FR-007**: 系统 MUST 记录 WindowSystem 的核心模块划分与职责边界（窗口系统管理、窗口对象封装、输入回调桥接、显示器信息同步）。
- **FR-008**: 系统 MUST 记录窗口系统主数据流（创建窗口、注册回调、事件轮询、窗口回收）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（GLFW 版本、CMake/C++标准、模块导出）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=WindowSystem, type=库, backend=通用, status=已启用。
- **IWindowSystem / IWindow**: 跨平台窗口系统与窗口对象抽象接口。
- **WindowSystem (GLFW)**: `IWindowSystem` 实现，维护窗口集合、显示器信息与输入回调函数。
- **WindowImpl**: `IWindow` 实现，封装单窗口生命周期与本地句柄访问。

## 目录结构解释

```text
WindowSystem/
├── CMakeLists.txt               # WindowSystem 模块与 Interface 构建，接入 GLFW
├── header/
│   └── CAWindow/
│       └── WindowSystem.h       # IWindow/IWindowSystem/MonitorInfo 公共接口
└── private/
	├── WindowSystem_Impl.h      # GLFW 窗口系统实现声明
	├── WindowSystem_Impl.cpp    # GLFW 回调桥接、窗口系统管理实现
	├── Window_Impl.h            # 单窗口实现声明
	├── Window_Impl.cpp          # 单窗口操作实现
	└── GLFWInclude.h            # GLFW 头与平台桥接
```

## 核心模块划分

1. 公共接口层
- 入口：`header/CAWindow/WindowSystem.h`
- 责任：定义窗口管理、输入事件、显示器信息和本地句柄访问协议。

2. GLFW 系统管理层
- 入口：`private/WindowSystem_Impl.cpp`
- 责任：初始化 GLFW、创建窗口、轮询事件、同步显示器列表并维护窗口容器。

3. 窗口实例封装层
- 入口：`private/Window_Impl.cpp`
- 责任：实现单窗口创建/销毁、尺寸与位置控制、输入状态查询与 native handle 暴露。

4. 回调分发桥接层
- 入口：`WindowSystem_ImplGlfw_*Callback` 静态回调族
- 责任：将 GLFW 回调转发到 `WindowSystem` 注册的业务回调。

## 数据流描述

### 数据流 A：窗口生命周期流程（主流程）

1. 调用 `IWindowSystem::NewWindow(...)` 创建 `WindowImpl`。
2. `WindowImpl::Initialize` 创建 GLFWwindow 并绑定用户指针。
3. `WindowSystem::InitializeWindowCallbacks` 注册所有输入与窗口回调。
4. 每帧 `UpdateSystem` 执行 `glfwPollEvents`、刷新显示器和窗口列表。
5. 关闭且仅剩系统引用的窗口会在 `UpdateWindows` 中回收。

### 数据流 B：输入事件回调流程（支撑流程）

1. GLFW 触发键盘/鼠标/焦点等回调。
2. 静态桥接函数通过 user pointer 取得 `WindowImpl`。
3. 桥接函数调用 `WindowSystem` 中对应的函数对象回调。
4. 上层系统得到统一 `IWindow*` 事件语义。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层标准配置）。
- GLFW：3.4（`glfw/glfw`，禁用 examples/tests/docs/install）。
- 目标形态：`WindowSystem` 为 MODULE；`WindowSystem_Interface` 为 INTERFACE。
- 直接依赖：`CACore`、`ThreadManager_Interface`、`glfw`。
- 模块导出：`CA_MODULE_INSTANCE(cawindow::IWindowSystem, cawindow::WindowSystem, WindowSystem_GLFW)`。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
