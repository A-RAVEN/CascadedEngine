# 功能规格说明：IMGUIContext AS-IS 基线

**Feature Branch**: as-is/IMGUIContext  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 IMGUIContext 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 IMGUIContext 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 IMGUIContext 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 IMGUIContext。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 IMGUIContext 的目录层级解释（覆盖 `header/IMGUIContext/` 与 `IMGUIContext.cpp`）。
- **FR-007**: 系统 MUST 记录 IMGUIContext 的核心模块划分与职责边界（平台输入桥接、多视口上下文、绘制数据准备、字体资源初始化）。
- **FR-008**: 系统 MUST 记录 IMGUI 主数据流（Context 初始化、输入回调注入、帧更新、DrawData 转图提交）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（ImGui/GLM/STB、CMake/C++标准、模块导出）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=IMGUIContext, type=库, backend=通用, status=已启用。
- **IMGUIContext**: IMGUI 渲染与事件桥接核心类，负责初始化、更新和绘制图数据构建。
- **IMGUIViewportContext**: 每视口运行时数据容器，维护顶点/索引缓冲、剪裁矩形和纹理绑定。
- **IMGUITextureViewContext**: 自定义视图纹理映射上下文，描述窗口、渲染目标与视口区域。

## 目录结构解释

```text
IMGUIContext/
├── CMakeLists.txt                       # IMGUIContext 静态库构建与依赖声明
├── header/
│   └── IMGUIContext/
│       ├── IMGUIContext.h              # 核心接口与视口/纹理上下文定义
│       └── IMGUIIncludes.h             # ImGui 头封装
├── IMGUIContext.cpp                    # 平台回调桥接、初始化与绘制主逻辑
└── stb_impl.cpp                        # stb_image/stb_image_write 实现入口
```

## 核心模块划分

1. IMGUI 运行时控制层
- 入口：`IMGUIContext`（`IMGUIContext.cpp` / `IMGUIContext.h`）
- 责任：创建/维护 ImGui Context，执行 NewFrame 与 DrawData 解析。

2. 平台输入桥接层
- 入口：`ImGui_ImplGlfw_*Callback` 回调族
- 责任：把窗口系统输入事件转换为 ImGui IO 事件。

3. 多视口与纹理视图层
- 入口：`IMGUIViewportContext`、`IMGUITextureViewContext`
- 责任：管理多视口 GPU 资源、窗口绑定与场景纹理展示。

4. 字体与资源初始化层
- 入口：`Initialize(...)`
- 责任：构建字体 atlas、上传字体纹理到 GPU，并注册窗口回调。

## 数据流描述

### 数据流 A：初始化流程（主流程）

1. `IMGUIContext::Init` 注入 `CRenderBackend` 与 `IWindowSystem` 依赖。
2. `Initialize` 创建 ImGui Context，开启 Docking/Viewports。
3. 读取字体数据并通过 `GPUGraph` 调度上传字体纹理。
4. 向窗口系统注册键鼠/焦点/窗口事件回调。

### 数据流 B：每帧绘制流程（支撑流程）

1. `UpdateIMGUI` 生成当帧 UI 数据。
2. `PrepareDrawData` 为每个视口准备顶点、索引、剪裁和 shader 绑定。
3. `Draw` 把 ImGui 绘制命令翻译为 `GPUGraph` 的渲染 pass。
4. 调用方将图提交给渲染后端执行。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（模块显式设置 `CXX_STANDARD 20`）。
- 目标形态：STATIC（`add_library(IMGUIContext STATIC ...)`，并通过 `CA_SETUP_MODULE` 参与模块体系）。
- 直接依赖：`Rendering`、`WindowSystem_Interface`、`ThreadManager_Interface`、`CAGeneralReourceSystem_Interface`、`TimerSystem_Impl`、`IMGUI`、`glm`、`stb`、`CACore`。
- 模块导出：`CA_MODULE_INSTANCE(imgui_display::IMGUIContext, imgui_display::IMGUIContext, IMGUIContext)`。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
