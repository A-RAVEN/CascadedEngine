# Design: io-error-channel

## Context

三层依赖链：IOManager 批 → CodeGen 读对象 → `TResource`/资源注册表。缓冲区来源的反序列化已有完整边界检查，流式分支没有——失败语义"由来源决定"（`CodeGenSerialization.h` 头注释自述），而来源（`IOBatch`）恰恰不报告。本批把这条链补成闭环：**来源报告 → 读对象传播 → 资源层抛出**。开工实测：实现方仅 `IOBatchImpl`/`WriteBatchImpl` 两个，CoreTests 桩两个（`Test/CoreTests/ResourceBatchTest.cpp` `WriteBatchStub`/`ReadBatchStub`）；生成代码经 `reader::read_raw` 统一调用 `Read(size, dest)`，是唯一的流式消费形态。

## Goals / Non-Goals

**Goals:**
- 越界读、文件打不开、尺寸探测失败、写目标打不开：全部显式失败，错误消息点名路径（必要时含原因）。
- 流式反序列化与缓冲区反序列化同一失败语义；生成物零改动、字节契约逐字节不变。
- 资源加载失败透出到调用方（后端初始化、导入、测试），不再静默灌垃圾。
- CoreTests 新增红绿用例 + tester 集成红绿（截断 shLib）。

**Non-Goals:**
- 不做真异步/并发读改造（`SubmitAndWait` 保持同步语义，scheduler 跳线程现状原样保留）。
- 不做读侧文件句柄缓存、chunk 合并逻辑重构。
- 不做 `SerializeAll` 增量回写、源删除清产物、资源身份（内容哈希/依赖记录）——ROADMAP 方向段的后几项，另批。
- 不绑 C#、不动绑定面、不动 ABI 版本。
- `CACore/header/FileLoader.h`（`cacore::LoadBinaryFile`/`LoadStringFile` 同步直读）与本批同族的静默失败缺陷不在本批，记入 ROADMAP 方向段后续。

## Decisions

**D1 错误通道形状：来源 bool 报告 + 顶层类型化异常。** `IOBatch::Read`/`WBatch::Write` 返回 `bool`（false = 本次读写被来源拒绝），同名模板便利重载 `Read(T&)`/`Write(T const&)` 同步改返回 `bool` 转发——现状它们 void 丢弃返回值，是公开入口上的第二个静默点。`IOBatch`/`WBatch` 新增纯虚 `SourcePath()`（返回来源路径），供上层在抛 `io_error` 时点名文件；实现方 2 个 + CoreTests 桩 2 个同步。`SubmitAndWait` 保持 void，其执行期失败以 `ca_io::io_error` 异常抛出。异常定义于 `Interface/IOManager/header/IOManager/IOManager.h`，派生 `std::runtime_error`。选异常不选状态码的理由：资源层三处顶层检查点（`TResource` 读/写、`GetOrLoadResource`、`SerializeAll`）都是"失败即中止当前操作"语义，调用方没有"部分继续"需求；状态码要求每处调用点新增检查且易被漏——现状 `Deserialize` 忽略 `deserialize` 返回值正是漏检的产物。跨模块抛异常：实现与消费同进程、同 CRT（模块 DLL 均用共享运行时），与项目现有异常用法（`CASTL/CAException.h`）一致。

**D2 工作线程内不抛异常 + 读写中途错误检查。** `SubmitAndWait` 的任务跑在 ThreadManager 线程上，任务体内把失败捕获为值（bool + 消息字符串），`WaitAll()` 返回后在调用线程统一抛 `io_error`。理由：任务系统的异常传播语义未经核实，不依赖未经核实的行为；错误消息在任务线程内定型，按值跨线程搬运。读写中途错误与"打不开"同列为执行期失败：读任务每个 chunk `read()` 后检查 `gcount()` 等于 chunk 尺寸（防 TOCTOU——创建批后源文件被截短造成的短读静默写零），写任务 `write()`/`flush()` 后检查流状态；两类失败同样按值捕获、调用线程抛 `io_error` 点名路径。

**D3 读对象流式分支补法（生成物零改动）。** `CodeGenSerialization.h` `read_raw` 流式分支：取 `Read` 返回值，false ⇒ `m_Failed = true; return false;`。生成代码调用形态不变（返回值本来就被忽略），生成的 `.codegen.h` 一字不动。`IOBatchImpl::Read` 越界改返回 false，保留"不推进批内位置"的行为。CoreTests `ReadBatchStub` 同步改：越界返回 false（注释「读越界时不动位置，让上层的读对象自己发现失败」的意图由此真正成立）。

**D4 尺寸探测时机与错误类型。** `IOBatchImpl` 构造函数现状裸调 `std::filesystem::file_size`（文件不存在即抛 `filesystem_error`）。改为：`IOManagerImpl::Batch(path)` 创建批时探测文件存在与尺寸，失败即抛 `io_error` 点名路径——加载入口第一时间报错，不拖到 Submit；探测结果存入 `m_FileSize` 供 `Read` 边界判断。`WBatch` 侧不做创建期探测（现状也不做），写失败统一在 `SubmitAndWait` 报。

**D5 资源层检查点。** `TResource::Deserialize`：`CodeGenSerializer<TRes>::deserialize` 返回 false ⇒ 抛 `io_error`；消息点名资源来源路径——`TResource` 自身不知道自己路径，来源路径经 D1 的 `SourcePath()` 访问器从批上取，资源层异常消息补充资源类型。`IResourceReadOnly` 全仓实测只有纯虚声明、无任何实现（2026-11-11 对抗裁决复核 verified），本批不改它的声明、spec 也不对它立要求。`ResourceManagingSystem_Impl::GetOrLoadResource`/`SerializeAll` 不加 try/catch，异常自然透出给后端与测试。`TResource::Serialize` 侧：写批失败已由 `WriteBatchImpl` 抛出，`Serialize` 不另加检查点（保持最小改动）。

**D6 桩同步与字节红线。** CoreTests 两个桩（`WriteBatchStub`/`ReadBatchStub`）签名同步改 bool 返回；`ResourceBatchTest` 现有四断言（新旧字节对拍、转发一致性、读回一致、反向对照）必须保持全绿——字节契约不变是本批红线，任何让这四断言变红的改法都是改错了。

## Risks / Trade-offs

- [有调用方依赖"越界静默"旧行为] → 0.1 全量 grep `IOBatch`/`WBatch` 实现与消费点清单；验收若发现依赖静默的路径，按 D1"失败显式化"为准修订该路径，不回退判据。
- [异常跨 ThreadManager 任务边界丢失] → D2 已隔离：任务内按值捕获，调用线程抛；新增用例覆盖"任务内打不开文件"红态。
- [创建期探测引入 TOCTOU（Batch 后文件被删）] → Submit 时 `is_open()` 失败仍按 D2 报 `io_error`，双保险，场景钉死。
- [集成红态依赖测试资产目录] → 6.x 验收先复制备份再截断 `VulkanShaderLibrary.shLib`，跑完还原；判据写明备份/还原步骤，日志存档。
- [DLL 边界异常类型识别] → `io_error` 定义在接口头（`Interface/IOManager`），实现与消费方都经同一头包含，类型身份一致；测试用 `catch (ca_io::io_error const&)` 验证。

## Migration Plan

1. 开工清点（0.1）：实现点/消费点 grep 清单，出现清单外实现点即修订 design 影响面。
2. 接口与运行时（D1/D3/D4）：`IOManager.h` + `CodeGenSerialization.h` + `IOManager_FS/IOManager.cpp`。
3. 资源层检查点（D5）：`IResource.h` + `ResourceImportingSystem.cpp` 走读确认无吞异常。
4. CoreTests 桩同步 + 新增红绿用例（D6 + 越界/截短/缺失文件三态）。
5. 全量构建 + CoreTests + 两后端 headless 16/16。
6. tester 集成红绿（截断 shLib，备份/还原）。
7. `IOManager_FS/README.md`（错误行为、示例、真实输出）。

## Open Questions

（无——错误通道形状、异常类型、线程边界、探测时机、桩同步均已定死。）
