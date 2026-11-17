# IOManager_FS

`ca_io::IOManager` 的**文件系统实现**（模块名 `IOManager_FS`，构建产物 `IOManager_FS.dll`）。
它提供两件事：

- `IOBatch`：把多次读请求攒成批，`SubmitAndWait()` 时在后端线程合并成大块读、再分发回各目标内存；
- `WBatch`：把多次写请求攒成批，`SubmitAndWait()` 时合并成大块写，写目标句柄按路径缓存。

接口声明在 `Interface/IOManager/header/IOManager/IOManager.h`；错误类型 `ca_io::io_error` 也定义在那里
（派生 `std::runtime_error`，消息点名路径与原因）。

## 做什么 / 不做什么

做：

- 读侧**创建期探测**源文件（是否存在、是不是目录、尺寸多少），失败立刻报；
- 读/写侧**执行期检查**（源文件打不开、短读、写目标打不开、写流失败）在**调用线程**抛 `ca_io::io_error`；
- `IOBatch::Read` / `WBatch::Write` 返回 `bool`，来源拒绝时调用方看得见；
- `SourcePath()` 让上层拼错误消息时能点名文件。

不做（本批明确不做，见 `openspec/changes/io-error-channel/design.md` 的 Non-Goals）：

- 不做真异步/并发读改造：`SubmitAndWait` 保持同步语义（内部仍用一个任务 + `WaitAll()`）；
- 不做读侧文件句柄缓存、chunk 合并逻辑重构；
- 不做增量回写、源删除清产物、资源身份（内容哈希/依赖记录）。

## 六种失败：改前 vs 改后

| # | 场景 | 改前（静默或未类型化） | 改后 | 实测读数 |
|---|---|---|---|---|
| 1 | **越界读**（批内位置 + 请求长度 > 文件尺寸） | `Read` 直接 `return`：请求被丢弃、目标内存留垃圾，调用方拿不到任何信号 | `Read` 返回 `false` 且**不推进**批内位置 | `evidence/1.3-绿态.txt`（越界读 16 字节被拒=1、拒绝后再读 8 字节仍被接受=1） |
| 2 | **创建期尺寸失败**（路径不存在、路径是目录） | 构造里裸调 `std::filesystem::file_size`，抛未类型化的 `std::filesystem::filesystem_error`（调用方认不出类型、消息里也没有路径） | `IOManager::Batch` 抛 `ca_io::io_error`，消息点名路径与原因 | `evidence/1.2-绿态.txt` |
| 3 | **提交期打不开源文件**（建批之后文件被删/权限变化） | 跳过整批读、不报错，`SubmitAndWait` 正常返回 | `SubmitAndWait` 在**调用线程**抛 `io_error`，原因「打不开源文件」，消息点名路径 | `evidence/1.4-绿态.txt` 的 1.4① |
| 4 | **中途短读**（建批之后源文件被截短） | 不检查 `gcount()`，把 stage 缓冲里的残渣静默 `memcpy` 进目标内存 | 同上；消息带「偏移 / 请求字节 / 实际字节」 | `evidence/1.4-绿态.txt` 的 1.4② |
| 5 | **写目标打不开**（父目录不存在、没有写权限等） | `is_open()` 为 false 时静默丢弃已排队的写，`flush()` 空转 | `WBatch::SubmitAndWait` 在调用线程抛 `io_error`，原因「打不开写目标」，消息点名路径 | `evidence/1.5-绿态.txt` |
| 6 | **写流失败**（`write()` / `flush()` 之后流状态变坏） | 不看流状态，写失败当成功 | 同上；原因「写失败（偏移 …）」或「flush 之后流状态变坏」 | 与第 5 行同一个检查函数（`WriteBatchImpl::SubmitAndWait` 里的 `if (!file_src.good())`）。**这一支本批没有独立的注入读数**：要制造"open 成功但 write 失败"需要磁盘满或只读句柄，测试里没有稳定手段 —— 如实登记，未编造读数 |

两条约定要一起读：

- **失败时不清空批里的读**：提交失败后没被填充的读仍在队列里（`evidence/1.4-绿态.txt` 的路径就是证据——
  失败发生在 `Clear()` 之前），所以调用方能看到"哪些读没做成"，而不是只收到一句"失败了"；
- **`SubmitAndWait` 保持 void**：执行期失败一律用异常报告；创建期失败（`Batch()`）也是异常。
  调用方要"部分继续"就得自己 catch，这与资源层三处检查点（`TResource` 读/写、`GetOrLoadResource`、
  `SerializeAll`）"失败即中止当前操作"的语义一致。

## 可复制示例

下面两段就是 `Test/GPUBackendTester/Main.cpp` 里 `TestIOErrorChannel` 的真实代码（已编译、已跑过），
不是另外编的演示程序：判据 1.2 与 5.1 是它们产生的。

```cpp
// 读：路径不存在时 Batch() 就抛，消息里点名路径与原因。
try
{
    auto batch = pIOManager->Batch("CAAssets/__io_error_channel_missing__.res");
}
catch (ca_io::io_error const& e)
{
    // e.what() = 批创建期探测源文件失败："…/CAAssets/__io_error_channel_missing__.res"（…）
}

// 读：越界读返回 false，且批内位置不推进（拒绝之后再读一段仍会被接受）。
auto batch = pIOManager->Batch(filePath);          // 16 字节的文件
unsigned char head[8] = {};
unsigned char overrunTarget[16] = {};
unsigned char tail[8] = {};
batch->Read(8, head);                              // true
batch->Read(16, overrunTarget);                    // false：8 + 16 > 16
batch->Read(8, tail);                              // true：位置没被推进
batch->SubmitAndWait();
```

```cpp
// 写：目标打不开时 SubmitAndWait 抛，消息里点名目标路径。
auto batch = pIOManager->WriteBatch("…/no_such_dir/output.bin");
unsigned char payload[16] = {};
batch->Write(16, payload);                         // 先排队（空批 SubmitAndWait 首行就返回）
batch->SubmitAndWait();                            // 抛 ca_io::io_error：批写提交失败："…/no_such_dir/output.bin"（打不开写目标）
```

## 真实运行输出（照抄归档读数，未手编）

来源：`openspec/changes/io-error-channel/evidence/1.2-1.5-5.1-绿态-tester日志-vulkan.log`（vulkan 与 d3d12
两份副本逐字相同）。归档日志里每行前面带 ANSI 颜色前缀（`ESC[38;2;128;128;128m`），下面照抄去掉前缀后的正文。

```
[I/O 错误通道 1.2] 不存在的路径在 IOManager::Batch 抛 ca_io::io_error 且点名路径：判据成立 ⇒ ca_io::io_error：批创建期探测源文件失败："C:\Users\76956\AppData\Local\Temp\io_error_channel_probe\missing_input.bin"（The system cannot find the file specified.）
[I/O 错误通道 1.3] 越界读返回 false、批内位置不推进、已接受的读仍读得到正确字节：判据成立 ⇒ 首读 8 字节被接受=1、越界读 16 字节被拒=1、拒绝后再读 8 字节仍被接受（位置未推进）=1、头 8 字节内容符合=1、尾 8 字节内容符合=1
[I/O 错误通道 1.4①] 排队读之后删除源文件，SubmitAndWait 抛 ca_io::io_error 点名路径：判据成立 ⇒ ca_io::io_error：批读提交失败："C:\Users\76956\AppData\Local\Temp\io_error_channel_probe\deleted_after_batch.bin"（打不开源文件）
[I/O 错误通道 1.4②] 排队读之后把源文件截短到 8 字节，SubmitAndWait 抛 ca_io::io_error 点名路径：判据成立 ⇒ ca_io::io_error：批读提交失败："C:\Users\76956\AppData\Local\Temp\io_error_channel_probe\truncated_after_batch.bin"（源文件读不足（偏移 0、请求 16 字节、实际读到 8 字节；创建批之后源文件被截短？））
[I/O 错误通道 1.5] 写目标打不开（父目录不存在）时 SubmitAndWait 抛 ca_io::io_error 点名路径：判据成立 ⇒ ca_io::io_error：批写提交失败："C:\Users\76956\AppData\Local\Temp\io_error_channel_probe\no_such_dir\output.bin"（打不开写目标）
[I/O 错误通道 5.1] 经 GetOrLoadResource 加载缺失资产路径抛 ca_io::io_error 点名该路径：判据成立 ⇒ ca_io::io_error：批创建期探测源文件失败："E:/Projects/CascadedEngine/CAAssets/__io_error_channel_missing__.res"（The system cannot find the file specified.）
```

改前同一批读数的存档（`evidence/1.2-1.5-5.1-红态-tester日志-vulkan.log`），用来对照"改前是静默的"：

```
[I/O 错误通道 1.2] 不存在的路径在 IOManager::Batch 抛 ca_io::io_error 且点名路径：判据不成立 ⇒ 未知类型的异常
[I/O 错误通道 5.1] 经 GetOrLoadResource 加载缺失资产路径抛 ca_io::io_error 点名该路径：判据不成立 ⇒ 未知类型的异常
```

（改前 1.2/5.1 抛出的是裸 `std::filesystem::filesystem_error`。`GPUBackendTester` 的目标带
`-D_HAS_EXCEPTIONS=0`（继承自 Jolt），那个模式下 `std::exception` 是 `stdext::exception`，
所以测试进程里连 `catch (std::exception const&)` 都匹配不上它，只落到 `catch (...)` —— 这也正说明
"未类型化的文件系统异常"对调用方等于不可识别。`ca_io::io_error` 的类型名两侧一致，不受影响。）

完好资产下的全量回归：`vulkan` 与 `d3d12` 各 17 条用例全部 `pass`（report：
`openspec/changes/io-error-channel/evidence/4.2-全量回归-report-vulkan.json` 与 `…-d3d12.json`）。

## 回归时怎么把这条链路弄红（自查）

- 把 `IOBatchImpl::Read` 的越界分支改回"静默接受"（`return true;`）⇒ 只有 1.3 那行判据不成立，
  整条 `TestIOErrorChannel` 判红（其它五行仍成立）；
- 删掉 `IOBatchImpl::SubmitAndWait` 里的 `gcount()` 检查 ⇒ 只有 1.4② 那行不成立；
- 把 `IOManagerImpl::Batch` 的创建期探测换回裸 `std::filesystem::file_size` ⇒ 1.2 与 5.1 两行都变成
  "不是 `ca_io::io_error`"；
- 删掉 `WriteBatchImpl::SubmitAndWait` 里的 `is_open()` 检查 ⇒ 只有 1.5 那行不成立。

命令（两后端各一次）：

```
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestIOErrorChannel --headless 1 --report test_output\io_error_channel_green_vulkan.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestIOErrorChannel --headless 1 --report test_output\io_error_channel_green_d3d12.json
```
