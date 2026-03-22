# 功能规格说明：CACore AS-IS 基线

**Feature Branch**: as-is/CACore  
**Created**: 2026-03-21  
**Status**: Draft  
**Input**: 当前仓库中模块 CACore 的基线状态

## 用户场景与测试 *(mandatory)*

### 用户故事 1 - 理解模块边界 (Priority: P1)

作为引擎开发者，我需要掌握 CACore 的当前基线，以便在不破坏模块边界的前提下进行修改。

**Why this priority**: 这是宪章原则 I 的前置要求。

**Independent Test**: 评审者仅通过本文件即可判断模块职责边界。

**Acceptance Scenarios**:

1. **Given** 一项计划中的改动，**When** 评审本规格，**Then** 能明确该模块的范围与边界。

---

### 用户故事 2 - 跟踪后端域与构建状态 (Priority: P2)

作为构建维护者，我需要知道 CACore 的后端域和构建参与状态，用于一致性与规划。

**Why this priority**: 支撑宪章原则 II 与 III。

**Independent Test**: 规格中的元数据可与顶层 CMake 配置交叉验证。

**Acceptance Scenarios**:

1. **Given** 顶层 CMake 配置，**When** 对照本规格，**Then** 类型与状态信息一致。

## 边界场景

- 子项目存在，但未被顶层 add_subdirectory 启用。
- 子项目职责可能跨多个领域，后续需要拆分。

## 需求 *(mandatory)*

### 功能需求

- **FR-001**: 系统 MUST 记录子项目名为 CACore。
- **FR-002**: 系统 MUST 记录目标类型为 库。
- **FR-003**: 系统 MUST 记录后端域为 通用。
- **FR-004**: 系统 MUST 记录构建状态为 已启用。
- **FR-005**: 系统 MUST 保留该基线作为后续规划输入。
- **FR-006**: 系统 MUST 记录 CACore 的目录层级解释（至少覆盖 `header/`、`private/`、`header/CACore/`、`header/CASTL/`）。
- **FR-007**: 系统 MUST 记录 CACore 的核心模块划分与职责边界（模块生命周期、反射序列化哈希、I/O、基础平台与日志）。
- **FR-008**: 系统 MUST 记录模块实例管理主数据流（模块加载、工厂注册、实例创建、链接、查询、释放）。
- **FR-009**: 系统 MUST 记录关键技术栈与版本来源（CMake/C++标准与核心第三方依赖版本）。

### 宪章对齐 *(mandatory)*

- **CA-001 Module Boundaries**: 记录模块归属与边界（原则 I）。
- **CA-002 Backend Parity**: 记录后端域以支持一致性追踪（原则 II）。
- **CA-003 Determinism**: 记录构建参与状态以支持确定性规划范围（原则 III）。
- **CA-004 Validation Gates**: 对行为变更要求先失败后通过的自动化验证（原则 IV）。
- **CA-005 Performance and Debuggability**: 对运行时关键路径要求可量化开销与诊断证据（原则 V）。

### 关键实体 *(若涉及数据则必须填写)*

- **SubprojectBaseline**: name=CACore, type=库, backend=通用, status=已启用。
- **ModuleFactoryRegistry**: 由 `IModuleFactory` 列表维护模块工厂注册，驱动实例化与链接流程。
- **InstanceRegistry**: `typeName -> (instanceName -> void*)` 的运行时实例索引，用于按接口类型检索模块实例。

## 目录结构解释

```text
CACore/
├── CMakeLists.txt                # CACore 构建与依赖声明
├── header/
│   ├── CACore/                   # 引擎核心接口层（模块管理、哈希、类型工具、容器桥接）
│   ├── CASTL/                    # STL 风格容器与并发封装（string/vector/map/mutex 等）
│   ├── FileLoader.h              # 文本/二进制文件读写接口
│   ├── library_loader.h          # Windows 动态库加载与模块管理实现（独立于 CAModuleManager）
│   ├── DebugUtils.h              # 日志、断言与错误输出宏
│   ├── Platform.h                # 平台抽象与 Windows 头封装
│   ├── Reflection.h              # 编译期反射描述与成员访问框架
│   ├── Serialization.h           # 基于反射的序列化/反序列化
│   └── Hasher.h                  # 聚合哈希、对象哈希包装与比较策略
└── private/
	├── CaModuleManager.cpp       # CAModule/CAModuleManager 生命周期与实例管理实现
	├── CAModuleImplementation.cpp# 模块导出样例与历史实现占位
	├── FileLoader.cpp            # 文件 I/O 具体实现
	├── LibraryLoader.cpp         # library_loader 命名空间实现
	├── MimallocImpl.cpp          # mimalloc 接入实现
	└── EASTL_Implementations.cpp # EASTL 相关实现桥接
```

## 核心模块划分

1. 模块生命周期与实例管理
- 入口：`header/CACore/CAModuleManager.h` + `private/CaModuleManager.cpp`
- 责任：加载模块、收集工厂、创建实例、执行链接、查询实例、统一释放。

2. 模块工厂与静态/动态导出桥接
- 入口：`header/CACore/CAModuleImplementation.h`
- 责任：通过 `CA_MODULE_INSTANCE` 注册工厂；在静态构建和动态库导出路径下统一接入 `IModuleManager`。

3. 反射/序列化/哈希基础设施
- 入口：`header/Reflection.h`、`header/Serialization.h`、`header/Hasher.h`、`header/CACore/CAHash.h`
- 责任：提供结构体成员访问、容器遍历、对象序列化与稳定哈希策略。

4. 文件与动态库 I/O 能力
- 入口：`header/FileLoader.h` + `private/FileLoader.cpp`，`header/library_loader.h` + `private/LibraryLoader.cpp`
- 责任：文本/二进制文件加载、DLL 加载、模块对象构造/销毁与接口名绑定。

5. 平台与调试支撑
- 入口：`header/Platform.h`、`header/DebugUtils.h`
- 责任：Windows 平台宏与 API 引入、日志与断言机制。

## 数据流描述

### 数据流 A：模块实例生命周期（主流程）

1. 调用 `CAModuleManager::AddModule(path)`。
2. `CAModule` 通过 `LoadLibrary` 装载模块并绑定 `TryInit/TryLink/TryRelease`。
3. `TryInit` 阶段将模块工厂注册到 `m_Factories`。
4. 调用 `CAModuleManager::LinkModules()`：
   - 遍历工厂创建实例并写入 `m_Instances`。
   - 调用 `LinkModuleInstance` 执行实例级依赖注入/初始化。
   - 调用每个模块的 `TryLink` 完成模块级联调。
5. 运行期通过 `GetInstance(typeName[, instanceName])` 查询实例。
6. 析构时先释放工厂实例，再 `Shutdown` 模块并卸载动态库。

### 数据流 B：对象哈希与序列化（基础设施流程）

1. 业务对象进入 `aggregateHasher` 或 `serializer`。
2. 对基础类型直接处理；对容器写入长度后递归处理元素；对类类型通过 `visit_members` 递归展开成员。
3. 哈希输出为 `size_t` 或 `sha256` 结果；序列化输出字节流或批量写入目标。
4. 反序列化按同一结构读取并重建对象状态。

## 关键技术栈版本

- CMake 最低版本：3.12（顶层 `cmake_minimum_required(VERSION 3.12)`）。
- C++ 标准：C++20（顶层 `set(CMAKE_CXX_STANDARD 20)`）。
- fmt：11.1.3（`fmtlib/fmt`）。
- mimalloc：v3.0.8（`microsoft/mimalloc`，`MI_USE_CXX=ON`，`MI_OVERRIDE=ON`）。
- magic_enum：0.9.7（`Neargye/magic_enum`）。
- komihash：5.10（`avaneev/komihash`）。
- hash-library：`hash_library_v8`（`stbrumme/hash-library`，用于 SHA256 等实现）。
- 平台 API：Windows `LoadLibrary/GetProcAddress/FreeLibrary` 动态加载链路。

## 成功标准 *(mandatory)*

### 可量化结果

- **SC-001**: 本子项目的基线元数据字段完整。
- **SC-002**: 评审者可在 60 秒内定位关键元数据。
- **SC-003**: 后续规划无需再次扫描该模块 CMake 即可获取基础信息。
