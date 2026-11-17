# Proposal: io-error-channel（I/O 批错误通道与资源反序列化失败显式化）

## Why

现状三层依赖链——`Interface/IOManager` 批式读写 → `IOManager_FS` 实现 → `GeneralResources` 资源注册表——在失败路径上全线静默，实测证据：

- `IOBatchImpl::Read`（`IOManager_FS/IOManager.cpp`）：「`if (m_Batchs.GetCurrentPos() + readSize > m_FileSize)`」紧邻「`return;`」——越界读静默丢弃，目标内存留垃圾；读任务打不开源文件（`is_open()` 为 false）同样无信号；构造函数裸调 `std::filesystem::file_size`，文件不存在时未类型化的 `std::filesystem::filesystem_error` 直接逃逸给调用方。
- `CodeGenSerialization.h` 读对象流式分支：「`const_cast<ByteBuffer*>(m_Buffer)->Read(size, dest);`」「`m_Position += size;`」「`return true;`」——无条件报告成功。缓冲区分支已有完整边界检查（「`if (size > m_Buffer->size() - m_Position)`」后「`m_Failed = true;`」「`return false;`」），缺的正是流式这条腿；头注释自述流式来源"越界会失败由来源决定"，而来源（`IOBatch`）恰恰不报告。
- `GeneralResources/header/CAResource/IResource.h` `TResource::Deserialize`：调用 `CodeGenSerializer<TRes>::deserialize(...)` 的返回值被忽略——截短/损坏的资产文件（如 `VulkanShaderLibrary.shLib`）把垃圾数据静默灌进 `ShaderLibrary`，随后以难以归因的方式在渲染路径爆掉。

用户 2026-11-11 explore 会话裁定优先级：I/O 层错误通道卡在最前——资源层与绑定层的任何"加载失败处理"都以它为地基；本批只做错误通道与反序列化边界检查两项，同族的大改（增量回写、假异步、身份重设计）按 `docs/ROADMAP.md` 阶段 5 方向段另批。

## What Changes

- **IOBatch/WBatch 错误通道**：`Read`/`Write` 返回 `bool`（来源自己报告本次读写是否被接受），同名模板便利重载 `Read(T&)`/`Write(T const&)` 同步改返回 `bool` 转发，不再丢弃失败；`IOBatch`/`WBatch` 新增来源路径访问器（`SourcePath()`），使上层抛出的 `io_error` 能点名文件；新增类型化错误 `ca_io::io_error`（派生 `std::runtime_error`，定义于 `Interface/IOManager/header/IOManager/IOManager.h`，消息点名路径与原因）。
- **`IOBatchImpl` 三处补失败**：越界读返回 false 且批内位置不推进（现状直接 `return`）；文件尺寸探测失败（路径不存在/是目录）在 `Batch()` 创建时抛 `io_error` 点名路径，不再裸抛 `filesystem_error`；`SubmitAndWait` 执行期打不开源文件按 design D2 在调用线程抛 `io_error`。
- **`WriteBatchImpl`**：目标打不开/写失败显式抛 `io_error` 点名路径（覆盖 `SerializeAll` 落盘路径）。
- **CodeGen 读对象流式分支**：`Read` 返回 false ⇒ 置失败位并返回 false——与缓冲区分支同一失败语义；生成代码调用形态不变，**生成物零改动、字节契约逐字节不变**。
- **资源层失败传播**：`TResource::Deserialize` 检查反序列化结果，失败抛 `io_error`（`IResourceReadOnly` 全仓无实现，不点名，见 tasks 修订记录第 10 条）；`ResourceManagingSystem_Impl::GetOrLoadResource`/`SerializeAll` 不捕获吞异常，加载失败透出给后端与测试。

## Capabilities

### New Capabilities

- `io-error-channel`：批式读写与资源反序列化的失败显式化（新增规范全文见 `specs/io-error-channel/spec.md`）。

### 核对后不动

- **生成器（`Tools/CodeGen/`）**：流式/缓冲区形状判别逻辑不动，只改 `CACore/header/CodeGenSerialization.h` 运行时一处的流式分支；生成物字节契约与产物基线不受影响（spec 场景钉死）。
- **`csharp-*` 三规范**：本批不触碰绑定面，ABI 版本不动。

## Impact

- 产品：`Interface/IOManager/header/IOManager/IOManager.h`、`IOManager_FS/IOManager.cpp`、`CACore/header/CodeGenSerialization.h`、`GeneralResources/header/CAResource/IResource.h`、`GeneralResources/private/ResourceImportingSystem.cpp`、`Test/CoreTests/ResourceBatchTest.cpp`（桩签名同步 + 新增红绿用例）、新增 `IOManager_FS/README.md`。
- 接口面：`IOBatch::Read`/`WBatch::Write` 返回类型 void→bool。实现方实测共 2 个（`IOBatchImpl`/`WriteBatchImpl`）+ CoreTests 桩 2 个（`WriteBatchStub`/`ReadBatchStub`）；消费方（生成代码模板 `read_raw`、既有调用点）以 0.1 全量 grep 清单为准，出现清单外实现点即修订本 Impact 节与 design。
- 行为变化（故意的）：越界读/打不开文件从"静默继续"变"显式失败"。`ShaderImporter_Vulkan` 的源目录 fallback 警告路径不走批读写，不受影响；若验收发现其它依赖静默行为的调用路径，按"失败显式化"为准修订该路径，不回退判据。
- 回归口径：`python build.py --config Debug` 构建通过；CoreTests 全绿（含新增红绿用例、现有 `ResourceBatchTest` 字节对拍四断言保持绿）；两后端 headless 17/17（现有 16 + 新增 `TestIOErrorChannel`，2026-11-16 `--list` 实测）。集成红态：经 tester 新增 `TestIOErrorChannel` 用例加载一个资源字典中不存在且磁盘上也不存在的资产路径，必须抛 `ca_io::io_error` 且消息含该路径（红），完好资产 17/17 全绿（绿）。
- 规范依据：`docs/ROADMAP.md` 阶段 5「I/O 与资源管理层的正确性基础」方向段（2026-11-11 用户指示记入）。
