# Tasks: io-error-channel

## 0. 开工清点

- [x] 0.1 实现点与消费点清单（全仓口径）：`git grep -n -E "public ca_io::(IOBatch|WBatch)|: public (IOBatch|WBatch)"` 与 `git grep -n -- "->Read(\|\.Read("` 与 `git grep -n "CodeGenSerializer<.*IOBatch\|deserialize" -- "*ShaderLibraryCodeGenAssertions.cpp"` 三条命令**不限目录**全仓清点；结果（命令 + 输出原文）存档 change 内 `evidence/0.1-清单.txt`。验收：清单必须包含 `CACore/header/CodeGenSerialization.h` 的流式消费点与两后端 `ShaderLibraryCodeGenAssertions.cpp` 的显式实例化点（2026-11-11 对抗裁决 verified 的既有清单漏掉了这两处）；出现清单外实现点即修订 proposal 的 Impact 节与 design 并在此记录。

## 1. I/O 批错误通道（design D1/D2/D4）

- [x] 1.1 接口层：`IOManager.h` 中 `IOBatch::Read`/`WBatch::Write` 返回类型 void→bool；同名模板便利重载 `Read(T&)`/`Write(T const&)` 改返回 `bool` 转发；`IOBatch`/`WBatch` 新增纯虚 `SourcePath()`；新增 `ca_io::io_error`（派生 `std::runtime_error`，构造收路径 + 原因）。验收：与 1.2-1.5、2.1、4.1 同一批落地后统一构建（见 4.2）——**本条不单独验收构建**（只改接口不改实现与桩必然编译错，2026-11-11 裁决已证）。
- [x] 1.2 `IOBatchImpl` 创建期探测：`IOManagerImpl::Batch(path)` 探测文件存在与尺寸（替代构造函数裸调 `std::filesystem::file_size`），失败抛 `io_error` 点名路径。**红绿落在 tester 新增 `TestIOErrorChannel` 用例**（CoreTests 只链接口、不加载 IOManager_FS 模块，已核实无法构造真批）。红态：对不存在的路径 `Batch()`，`catch (ca_io::io_error const&)` 且 `what()` 含路径——改前红（抛 `std::filesystem::filesystem_error`，类型对不上），改后绿。
- [x] 1.3 `IOBatchImpl::Read` 越界：超出 `m_FileSize` 返回 `false` 且批内位置不推进（改前直接 `return`）。红绿在 `TestIOErrorChannel`：真临时文件（16 字节）建批，顺序读完 16 字节后再 `Read(1, &b)`，返回值必须为 `false`——改前红（返回 void 无从判），改后绿。
- [x] 1.4 `IOBatchImpl::SubmitAndWait`：任务体内打不开文件与 **`read()` 后 `gcount()` 不等于 chunk 尺寸（短读）** 均按 design D2 捕获为值，`WaitAll()` 后在调用线程抛 `io_error` 点名路径；任务体内不抛异常。红绿在 `TestIOErrorChannel`，两个红态：①批里**先排队一次读**再删除源文件再提交（空批首行即返回、到不了打开文件分支——必须先排队，2026-11-11 裁决已证），必须抛；②批里排队读前 16 字节后把源文件截短到 8 字节再提交（创建期尺寸仍按原长，越界检查挡不住），`gcount()` 短读必须抛。两红态改前均静默，改后必抛。
- [x] 1.5 `WriteBatchImpl::SubmitAndWait`：目标打不开（`is_open()` false）与 **`write()`/`flush()` 后流状态失败** 在调用线程抛 `io_error` 点名路径。红绿在 `TestIOErrorChannel`：先 `Write()` 排队一次写（空批首行即返回——必须先排队），再对不存在目录下的路径提交，必须抛——改前红（静默丢弃），改后绿。

## 2. 读对象流式分支（design D3）

- [x] 2.1 `CodeGenSerialization.h` `read_raw` 流式分支：取 `Read` 返回值，`false` ⇒ `m_Failed = true; return false;`；缓冲区分支一字不动。红绿在 CoreTests：`ReadBatchStub`（越界返回 false）喂截短载荷，`CodeGenSerializer<T>::deserialize` 必须返回 `false`——改前红（无条件 `true`），改后绿；`GeneratedContainerTest`/`FriendLikeTest`/`ShaderLibraryBytesTest` 等缓冲区用例全绿不动。

## 3. 资源层检查点（design D5）

- [x] 3.1 `IResource.h` `TResource::Deserialize`：`deserialize` 返回 `false` ⇒ 抛 `ca_io::io_error`（消息含来源路径——经 1.1 的 `SourcePath()` 从批上取）。红绿在 CoreTests（stub 驱动，不需要真模块）：把 `ResourceBatchTest` 的完整载荷截掉末字节喂 `TResource::Deserialize`，必须抛——改前红（字段垃圾、无报错），改后绿。
- [x] 3.2 `ResourceImportingSystem.cpp`：`GetOrLoadResource`/`SerializeAll` 走读确认无吞 `io_error` 的 catch 块（**不得新增 try/catch**）。验收可失败：`git grep -n "catch" -- GeneralResources/private/ResourceImportingSystem.cpp` 输出存档 evidence；逐条走读确认不吞 `io_error`；红态 = 实现中出现 `catch (...)` 或捕获 `io_error` 且不 rethrow——出现即本条红。

## 4. CoreTests 同步、产物基线与全量回归

- [x] 4.1 桩签名同步：`WriteBatchStub`/`ReadBatchStub` 改 bool 返回（越界/空写语义不变）并实现 `SourcePath()`；`ResourceBatchTest` 现有四断言（新旧字节对拍、转发一致、读回一致、反向对照）保持绿。验收：CoreTests 全绿，四断言读数存档。
- [x] 4.2 全量构建 + CoreTests + 两后端 headless 17/17（现有 16 个用例 + 新增 `TestIOErrorChannel`；2026-11-16 主循环 `--list` 实测现有 16 个；`docs/testing.md` 清单同步）。验收：`python build.py --config Debug` 通过；CoreTests 全绿；vulkan/d3d12 各 `--headless 10 --report` 全过（17 个），result.json 存档。
- [x] 4.3 CodeGen 产物逐字节不变双判据：① `python Tools/CodeGen/check_binding_baseline.py` 通过（bindings 基线）；② 构建前快照已由主循环完成（2026-11-16，77 份 `*.codegen.h` → `%TEMP%\ioerr-codegen-before`，保留相对目录结构），最终构建后逐个 `fc /b` 比对全同（2026-11-11 裁决已证 bindings 基线不覆盖 C++ 生成物，需单独比对）。验收：两判据输出存档 evidence；任一处字节差异即红。

## 5. tester 集成红绿

- [x] 5.1 经 `TestIOErrorChannel` 触发 `GetOrLoadResource` 加载一个资源字典中不存在且磁盘上也不存在的资产路径（2026-11-11 裁决已证"截断 shLib"红态不可达：tester 启动时 `ScanSourceDirectory` 无条件重建并重写该文件，且导入阶段已把 `ShaderLibrary` 建进内存字典、同进程内 `GetOrLoadResource` 命中内存不读盘），必须抛 `io_error` 且 test_output 日志点名该路径（红）；完好资产下两后端 17/17 全绿（绿，现有 16 + 新增 1）。验收：红绿两态日志 + result.json 存档 change 内 `evidence/`。

## 6. 对外文档

- [x] 6.1 新增 `IOManager_FS/README.md`：错误行为表（越界读/创建期尺寸失败/提交期打不开/中途短读/写目标打不开/写流失败六行，各配"改前静默、改后 io_error"对照）、可复制示例代码 + 真实运行输出（照抄 5.1 红态与 1.x 绿态的实测存档读数，不得手编）。验收：文件存在、六行行为表齐备；示例输出与 evidence 存档一致。

## 修订记录

（执行期间对 proposal/design/specs/tasks/ROADMAP 的任何修订，逐条记在此处：改前原文 / 改后原文 / 依据 / 谁裁定。）

### 2026-11-11 对抗验证后修订（主循环按裁决全量处置，裁决文件 `%TEMP%/adversarial-review-20261111-221330/裁决-全量.json`，27 条复核全 verified）

1. **tasks 0.1 grep 漏扫（裁决 misleading）**：改前——命令限定 `Interface/IOManager IOManager_FS GeneralResources Test` 四目录；改后——三条命令不限目录全仓清点，验收必须含 `CodeGenSerialization.h` 流式消费点与两后端 `ShaderLibraryCodeGenAssertions.cpp` 实例化点。依据：复核 verified 四目录 grep 0 命中、唯一流式消费点在 CACore。
2. **tasks 1.1 构建验收矛盾（裁决 blocking）**：改前——"先改齐 1.2/1.4 再统一构建"且 1.1 单独验收构建；改后——明写"本条不单独验收构建，与 1.2-1.5/2.1/4.1 同批落地后统一构建（见 4.2）"。依据：void→bool 波及 1.3/1.5/4.1 的 override 与桩，提前构建必败（verified）。
3. **模板便利重载丢返回值（裁决 blocking）**：改前——1.1 只改虚函数；改后——1.1 增改 `Read(T&)`/`Write(T const&)` 返回 bool 转发；spec Requirement 1 与 proposal/design D1 同步增句。依据：`IOManager.h` 模板重载丢返回值，公开入口失败仍静默（verified）。
4. **io_error 拿不到路径（裁决 blocking + contradictoryPairs）**：改前——design D5"路径由 IOBatchImpl 在异常消息里点名"，但越界 Read 只返 false、TResource 抛异常时无路径来源；改后——D1 增纯虚 `SourcePath()`（实现 2 + 桩 2 同步），D5 改"经 SourcePath() 从批上取"；spec Requirement 1/3 同步增句；tasks 1.1/4.1 同步。依据：`IOBatch` 无路径访问器（verified）。
5. **1.4 空批红态不可达 + 中途短读无落点（裁决 blocking×2 + contradictoryPairs）**：改前——1.4 红态"对创建后删除的文件提交"，未排队读，空批首行即返回；spec"读写错误"类别无任务落点；改后——1.4 红态①先排队读再删文件、②排队读后截短源文件（TOCTOU 短读，`gcount()` 检查必抓）；design D2 增读写中途错误检查；1.5 同步增"先 Write 排队"与写流状态检查；spec Requirement 1 执行期失败枚举补"读任务 gcount() 短读与写任务流状态失败"。依据：`IsEmpty()` 首行返回（verified）。
6. **1.2-1.5 红绿位置从 CoreTests 移到 tester（裁决 blocking）**：改前——1.2/1.3/1.4/1.5 均写"CoreTests 新增用例"；改后——实现侧红绿全部落到 tester 新增 `TestIOErrorChannel`（CoreTests 只链接口、不加载 IOManager_FS 模块，按字面写不出能调 `Batch()` 的用例，verified）；2.1/3.1 保持 CoreTests（stub 驱动不需要真模块）。spec 场景不点名测试位置，不用改。
7. **5.1 截短 shLib 红态不可达（裁决 blocking×2）**：改前——"备份→截短 shLib→headless 非零退出点名→还原→git status 无残留"；改后——缺失资产路径加载必抛 `io_error` 点名路径（红）+ 完好 16/16（绿）；删除 git status 判据（CAAssets 被 .gitignore 忽略，恒真，verified）与 64 字节截短（无出处，verified）。依据：tester 启动无条件 `ScanSourceDirectory` 重写 shLib（`Main.cpp` 已核），且导入阶段内存字典已有 `ShaderLibrary`，同进程 `GetOrLoadResource` 命中内存不读盘（verified）。spec Requirement 3 场景同步改「缺失的资产文件加载被点名拒绝」，并注明截短/损坏由 Requirement 1/2 链覆盖。
8. **3.2 守护恒真（裁决 blocking）**：改前——"grep try 输出存档"，命中子串也算完成；改后——grep 改 `catch`，逐条走读不吞 `io_error`，红态=出现 `catch (...)` 或吞 `io_error`。依据：实跑命中 3 行全为 entry 子串、无 try 语句（verified）。
9. **产物基线场景无任务（裁决 blocking + contradictoryPairs）**：改前——spec Requirement 2 场景「生成物字节契约不受影响」无任何任务；改后——新增 4.3：bindings 基线脚本 + 构建前后六模块 `.codegen.h` 逐字节 `fc /b` 双判据。依据：bindings 基线不覆盖 C++ 生成物（verified）。
10. **IResourceReadOnly 无落点（裁决 blocking + contradictoryPairs）**：改前——spec Requirement 3 与 tasks 3.1/design D5 点名「`IResourceReadOnly` 读侧同」；改后——删除该点名（全仓实测只有纯虚声明、无实现，verified），spec 加注不点名它。这是删无对象要求，非放松判据。
11. **proposal/design 引文非逐字（裁决 misleading）**：改前——三处多行原文并成单行引文；改后——按行拆开逐字引用。依据：`git grep -F` 并行引文 0 命中（verified），行号位置本身正确。
12. **「design 影响面」无此节（裁决 misleading）**：改前——0.1"修订 design 影响面"；改后——"修订 proposal 的 Impact 节与 design"。依据：design.md 无该章节（verified）。
13. **FileLoader 在 ROADMAP 无落点（裁决 misleading×3）**：改前——design Non-Goals 称"记入 ROADMAP 方向段后续"但 ROADMAP 无 FileLoader；改后——`docs/ROADMAP.md` 阶段 5 方向段后续项补入 `FileLoader.h` 同族静默失败修复（缺陷原文已核实：打不开静默返回空/丢写）。依据：ROADMAP 对 FileLoader 0 命中（verified）。
14. **6.1 示例输出来源（裁决 blocking）**：改前——"与 1.x 用例实测一致"无存档路径；改后——"照抄 5.1 红态与 1.x 绿态的实测存档读数，不得手编"。依据：原判据不可复现（verified）。

### 2026-11-16 开工前修订（主循环，apply 启动准备）

15. **用例总数 16→17（数字纠正，非放松）**：改前——tasks 4.2/5.1、proposal 回归口径、spec「完好资产加载不受影响」场景均写"16/16"，与"新增 `TestIOErrorChannel` 计入清单"自相矛盾；改后——统一为 17/17（现有 16 + 新增 1），并注明 2026-11-16 主循环 `GPUBackendTester.exe --list` 实测现有 16 个（输出已核）。依据：产品现有数字（--list 实测）+ 本 change 新增 1 个用例；判据加严（新用例也必须绿）。
16. **proposal 两处修订遗漏补齐**：改前——What Changes 仍写"（与 `IResourceReadOnly` 读侧）"、Impact 仍写"修订 design 影响面"；改后——前者删点名并注明无实现（对齐修订记录第 10 条），后者改"修订本 Impact 节与 design"（对齐第 12 条）。依据：2026-11-11 裁决 blocking/misleading，当时只改了 design/spec/tasks 漏了 proposal。
17. **4.3 快照已由主循环完成**：改前——"构建前把…拷贝到 %TEMP% 存档"（待执行动作）；改后——注明快照已完成（2026-11-16，77 份 → `%TEMP%\ioerr-codegen-before`，保留相对目录结构），执行者只需在最终构建后做 `fc /b` 比对。依据：主循环实测拷贝输出"快照份数: 77"。
