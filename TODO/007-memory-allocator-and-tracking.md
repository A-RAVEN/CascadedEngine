# 007 · 统一内存分配器与分配追踪

- **状态**：探索中（用户 2026-11-23：「内存管理的细节可以之后再聊」，本篇只记本次探索已达成的共识与待决点）
- **日期**：2026-11-23
- **关联**：已归档 `csharp-reverse-interface-binding`、`resource-importer-refactor`（资源所有权现状）；`docs/ROADMAP.md`（属引擎基础设施，不阻塞阶段 5 的 C# 框架层）

## 1. 背景与目标

用户长期印象：C++ 内存分配低效、易碎片化，因此克制用 `new`；旧资源系统曾拿 vector 当内存池定位构造资源对象，用户判断「有问题也很鸡肋」。本次探索回答三件事：

1. 旧 vector 内存池还在不在 → **已不在**（见 §2.1）。
2. 其它引擎怎么处理内存 → 分层策略，克制的是分配**模式**不是 `new` 关键字（见 §3）。
3. 对象内部 std 容器 / 外部库自己做的分配能否追踪归属 → **能**，守出口 + 调用栈 / 标签 / 计数分配器（见 §4）。

目标：把结论沉淀下来，细节（选型、跨 DLL 方案、接入开关）之后再聊；收敛后可转 proposal。

## 2. 现状核实（已核事实，带来源）

### 2.1 旧 vector 内存池已删除

- 旧实现 = `ResourceManagingSystemImpl::ChunkedMemoryAllocator`，位于 `GeneralResources/private/ResourceImportingSystem.cpp`（提交 `b3ce9a0~1` 可读）：
  - `castl::vector<castl::vector<uint8_t>> m_MemoryPages` 当内存页；每页 16 块（`ChunkedMemoryAllocator{ 16, size_in_bytes }`）；块大小 = 该类型**首次**分配的 `sizeof(TRes)`，此后钉死。
  - 按 `typeid(TRes).name()` 字符串分类型建池（`m_TypeNameToMemoryAllocator`）。
  - 旧 `ResourceManagingSystem.h` 模板 `AllocResource` / `AllocSubResource`：`AllocResourceMemory(...)` 拿块后 `new (address) TRes(...)` 定位构造；地址↔路径双向 map（`m_AssetPathToResourceAddress` / `m_ResourceAddressToAssetPath`），裸 `IResource*` 外借。
- 删除于提交 `b3ce9a0`「new resource manager」（经合并 `43d7799` 进主线）。当前源码树 `ChunkedMemoryAllocator` / `AllocResourceMemory` / `ReleaseResourceMemory` / 定位构造 `new (` 均 **0 处**（grep 实测，2026-11-23）。
- 仓库历史从未有过叫 `AssetManager` 的类型（`git log --all -S 'AssetManager'` 空）；用户记忆对应的是上面这套带 Asset 成员名的旧实现。

### 2.2 现在的资源所有权形态

- `ResourceManagingSystem_Impl`（`GeneralResources/private/ResourceImportingSystem.cpp`）：`castl::shared_dic<cacore::PathHash, castl::shared_ptr<IResource>> m_PathToResource`；构造走 `new TRes()` + `delete` 删除器，或注册式工厂 `Create()`；注册表是单一强所有者。
- 跨模块 / 脚本引用走句柄 + 世代索引（绑定句柄表），不外借裸指针。

### 2.3 活着的池与 GPU 分配

- `CACore/header/ThreadSafePool.h` 的 `TThreadSafePointerPool`：`castl::list<T>` 按节点构造 + 空闲指针 deque，**不是** vector 当内存。使用者：`ThreadManager/private/ThreadManager_Impl.h`（三个任务池）、`IMGUIContext/header/IMGUIContext/IMGUIContext.h`（视口上下文池）——高频定长对象，池化用对了地方。
- GPU 显存已走子分配：Vulkan 侧 VMA（`VulkanMemoryManager`）、D3D12 侧 `MemoryManager`，不逐资源调底层分配。

### 2.4 架构约束

- 多 DLL 模块架构（`CA_MODULE_INSTANCE`）：各模块若各带一份 C 运行时（CRT）堆，exe 里覆写 `new` 管不到 DLL 内部分配——统一分配器必须对所有模块可见（接入方案未定，见 §6）。
- castl 容器带分配器接口（EASTL 风格），可插跟踪分配器而不改容器。

## 3. 关键决策轴与结论

> 标注：〔共识〕= 本次讨论双方认可；〔建议〕= 我提出、用户尚未拍板。

1. **〔共识〕资源不做内存池**：资源是少量、大块、长命、从盘上加载的对象，分配耗时相对 IO 是噪声；旧池收益约等于零，却背上块大小钉死、对齐不足、手工析构配对、typeid 键跨模块不稳、裸指针悬空五类风险。
2. **〔共识〕克制的是模式，不是关键字**：真正致碎片 / 致慢的是长短命对象混堆、每帧大量临时分配、对象散落各处。现代尺寸分级分配器（mimalloc / tcmalloc / jemalloc）本身快、碎片有界。
3. **〔共识〕分层策略**（其它引擎的通用做法）：
   - 通用堆：尺寸分级分配器整体替换系统 malloc（Unreal 的 FMallocBinned2/3 同思路）；
   - 每帧临时：线性分配器（撞针分配、整帧一次性重置，零碎片；Unreal 帧分配器、Unity 的 Allocator.Temp）；
   - 高频定长对象：池 / 定长块复用（本引擎任务池已有）；
   - 实体与组件：ECS 定长块连续存储，不逐个 new（Unity DOTS、EnTT 稀疏集）；
   - 长命共享资产：引用计数、单一强所有者（本引擎资源注册表已有）；
   - 跨模块 / 脚本：句柄 + 世代索引（本引擎已有）；
   - GPU：子分配 + 别名（VMA / D3D12MA，本引擎已用）。
4. **〔建议〕性价比第一步 = 统一分配入口 + Tracy**：全局 `new`/`delete` 与 castl 分配器统一接到 mimalloc，再钩上 Tracy——速度、统计、泄漏可见性一次拿齐；之后**用数据**决定哪里真需要线性分配器或池，不预先造池。
5. **〔共识〕对象内部分配可追踪**：std 容器默认走全局 `operator new`，外部库多数走 malloc 或自带分配器回调——守出口即全捕获，容器和库一行不用改。归属三招：
   - **调用栈**：Tracy 给每笔分配记完整栈，std::vector 内部分配的栈上也有用户对象的成员函数帧，按栈分组即知归属；
   - **标签作用域**：进子系统前设线程局部标签，期间该线程分配都带标签（Unreal 低级内存跟踪器 LLM 的思路）；异步任务须在提交时捕获标签、执行时恢复，否则线程池里标签会串；
   - **按对象计数**：std::pmr 容器 + 计数 memory_resource（一对象 / 一子系统一份）；castl 容器用分配器接口同法。
   - 例外：自带私有内存区或直接系统调用（VirtualAlloc 等）的库，钩子看不见，需 OS 级 ETW 跟踪或库自身统计，且只能归到整个库。
6. **〔共识〕分析工具选型面**（成熟方案，细节之后再聊）：
   - 分配统计 + 调用栈 + 存活集合：Tracy（退出时仍存活 = 疑似泄漏）；
   - 子系统归属：LLM 式标签分配器；
   - 碎片化：VMMap（虚拟地址空间空洞）、Tracy 内存映射、mimalloc 自带 `mi_stats_print`；
   - 泄漏：MSVC ASan（`/fsanitize=address`）、CRT 调试堆 `_CrtDumpMemoryLeaks`、UMDH（需 gflags +ust）、Deleaker；
   - GPU：VMA 自带统计与分配转储。
   - 文档来源：[Tracy 内存跟踪](https://github.com/wolfpld/tracy/blob/f2a4f2529e7e71844c9e434b877770a21ecf7587/profiler/src/achievements/Memory.md)、[Unreal LLM](https://dev.epicgames.com/documentation/zh-cn/unreal-engine/using-the-low-level-memory-tracker-in-unreal-engine/?application_version=5.6#1)、[mimalloc 统计实现](https://chromium.googlesource.com/external/github.com/kripken/emscripten/+/d96ef72b34829eb04539a00dce35959639879f87/system/lib/mimalloc/src/stats.c#2)。

## 4. 被反驳 / 排除的结论

| 结论 | 处置 | 依据 |
|---|---|---|
| 给资源对象做 vector 内存池 | 排除，不重引入 | 旧实现已于 `b3ce9a0` 删除；长命低频对象池化收益约等于零 + 五类生命周期风险（§3.1） |
| 「一律克制 new」当一刀切规则 | 修正 | 致碎片 / 致慢的是分配模式不是关键字；现代分配器快且碎片有界（§3.2） |

## 5. 对抗验证结论

未跑（本篇是设计笔记，不是 change 工件；§2 事实均经 grep / git 历史实测，来源已列）。

## 6. 未决问题（细节之后再聊）

1. **分配器选型**：mimalloc vs tcmalloc vs jemalloc——未实测对比。
2. **跨 DLL 可见方案**：核心 DLL 导出统一入口 vs 逐模块接入 vs mimalloc 整体替换；须先核各模块 CRT 链接方式（/MD 还是 /MT）。
3. **每帧线性分配器与子系统标签**：是否引入、粒度多大（按模块？按资源类型？）。
4. **按对象计数通道**：std::pmr 还是 castl 分配器接口，或两者并存。
5. **Tracy 接入形态**：构建集成方式、编译开关（仅 Debug 还是全配置）、与现有日志 / 测试体系的关系。
6. **排期**：属引擎基础设施，与阶段 5（C# 框架层）无依赖；何时立项未定。

## 7. 升级建议

未决 1 / 2 / 5 收敛（选型 + 跨 DLL 方案 + 开关形态）后，可转 proposal（候选名 `memory-allocator-foundation`）。验收必须能失败，例如：Tracy 能捕获 std 容器内部分配并显示用户代码调用栈；构造一处故意泄漏，退出时存活集合能把它点名。