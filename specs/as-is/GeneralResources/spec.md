# 功能规格说明：GeneralResources AS-IS 基线

**Feature Branch**: as-is/GeneralResources  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 GeneralResources 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 GeneralResources 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 GeneralResources 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 GeneralResources。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 GeneralResources 的目录层级解释（覆盖资源接口头与 `ResourceImportingSystem.cpp` 实现）。
- **FR-007**: 系统 MUST 记录 GeneralResources 的核心模块划分与职责边界（资源导入系统、资源管理系统、工厂抽象、导入器协议）。
- **FR-008**: 系统 MUST 记录资源导入与持久化主数据流（扫描源目录、触发导入、序列化写入、按需加载）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（CMake/C++标准、模块导出与依赖）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=GeneralResources, type=库, backend=通用, status=已启用。
- **ResourceImportingSystem**: 导入编排接口，负责导入器注册与源目录扫描。
- **ResourceManagingSystem**: 资源持久化与缓存管理接口，负责 Serialize/Deserialize 与路径映射。
- **ResourceImporterBase / ResourceImporterFree**: 资源导入插件协议，分别用于后缀驱动与自由遍历导入策略。

## 目录结构解释

```text
GeneralResources/
├── CMakeLists.txt                       # 资源系统模块与 Interface 构建
├── header/
│   └── CAResource/
│       ├── IResource.h                  # 资源序列化/反序列化基础接口
│       ├── ResourceImporter.h           # 导入器协议定义
│       ├── ResourceImportingSystem.h    # 资源导入系统接口
│       ├── ResourceManagingSystem.h     # 资源管理系统接口
│       └── ResourceSystemFactory.h      # 资源系统工厂接口
└── private/
		└── ResourceImportingSystem.cpp      # 导入系统与管理系统实现、模块导出
```

## 核心模块划分

1. 资源导入编排层
- 入口：`ResourceImportingSystemImpl`
- 责任：注册导入器、扫描源目录、按后缀分派导入任务、触发批量导入。

2. 资源存储管理层
- 入口：`ResourceManagingSystem_Impl`
- 责任：维护资源根目录、资源缓存字典、资源序列化写入与按需反序列化加载。

3. 导入器协议层
- 入口：`ResourceImporterBase`、`ResourceImporterFree`
- 责任：定义导入器扩展点，支持类型化后缀导入与目录级自由导入。

4. 工厂与模块封装层
- 入口：`ResourceFactoryImpl`
- 责任：创建/销毁导入系统与管理系统实例，供模块化加载场景使用。

## 数据流描述

### 数据流 A：资源导入流程（主流程）

1. 注册导入器后调用 `ScanSourceDirectory(sourceRoot)`。
2. 系统遍历源目录并按文件后缀匹配导入器。
3. 对目标资源比较时间戳，筛出需要重新导入的文件。
4. 执行导入器 `ImportResource` 写入资源管理系统。
5. 完成后调用 `SerializeAll` 将资源落盘到资源根目录。

### 数据流 B：资源读取流程（支撑流程）

1. 业务调用 `GetOrLoadResource(path)`。
2. 资源管理系统在共享字典中查找缓存；未命中则创建资源对象。
3. 通过 `IOManager::Batch` 读取并反序列化资源。
4. 返回缓存化资源实例供后续复用。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层标准配置）。
- 目标形态：`CAGeneralReourceSystem` 为 MODULE；`CAGeneralReourceSystem_Interface` 为 INTERFACE。
- 直接依赖：`CACore`、`ThreadManager_Interface`、`IOManager`。
- 文件系统能力：`std::filesystem` 用于扫描目录与比较时间戳。
- 模块导出：
	- `CA_MODULE_INSTANCE(resource_management::ResourceImportingSystem, resource_management::ResourceImportingSystemImpl, ResourceImportingSystem)`
	- `CA_MODULE_INSTANCE(resource_management::ResourceManagingSystem, resource_management::ResourceManagingSystem_Impl, ResourceManagingSystem)`

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
