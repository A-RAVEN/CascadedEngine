# CACore

`CACore` 是最底层静态库：模块框架（`CAModuleManager`）、CASTL 容器别名、CodeGen 运行时入口与文件读写工具。
本文件只写对外可见的能力；它与代码同目录，改动要在同一次提交里更新（`CLAUDE.md` 的「文档维护」一节）。

## FileLoader（`CACore/header/FileLoader.h`）

三个函数：`LoadStringFile` / `LoadBinaryFile` / `WriteBinaryFile`。失败一律抛
`cacore::file_error`（派生 `std::runtime_error`，定义在同一个头文件里），消息形状与 `ca_io::io_error`
一致：`动作："路径"（原因）`。类型独立 —— CACore 不为复用异常类型反向依赖 Interface/IOManager 与
ThreadManager。

**当前无活跃调用方**（2026-11-21 实测：除三函数自身的定义处外，全仓唯一命中是
`ShaderProcessor/Main.cpp:18` 的 `fileloading_utils::LoadStringFile`，而该命名空间全仓不存在、ShaderProcessor
不在构建里 —— 构建外死代码，本批不碰）。失败语义由 `Test/CoreTests/FileLoaderErrorTest.cpp` 钉死。

### 错误行为表（改前 vs 改后）

| # | 场景 | 改前（静默） | 改后（`cacore::file_error`） |
|---|---|---|---|
| 1 | 读目标打不开（`LoadStringFile` / `LoadBinaryFile`） | `is_open()` 为假 → 静默返回空容器 | 抛：`加载文本文件："<路径>"（无法打开文件）` / `加载二进制文件："<路径>"（无法打开文件）` |
| 2 | 读取不足（二进制按 `tellg` 尺寸读后 `gcount()` 不足；文本流状态坏且非正常结束） | 不查 `gcount()` / 流状态 → 未填充的缓冲被当成完整内容返回 | 抛：原因「实际读到的字节数少于文件尺寸」/「读取未完成（流状态异常且未到文件末尾）」 |
| 3 | 写目标打不开（父目录不存在、无写权限等） | 静默丢写，`close()` 空转 | 抛：`写入二进制文件："<路径>"（无法创建或打开目标文件）` |
| 4 | 写流失败（`write()` / `flush()` / `close()` 后流状态坏） | 不看流状态，写失败当成功 | 抛：原因分别为「写入失败」/「刷新缓冲区失败」/「关闭文件失败」 |
| 5 | 空文件（0 字节） | 返回空容器 | 返回空容器，**不抛**（合法输入） |

两支没有独立注入读数（如实登记）：读取不足与写流失败要制造「open 成功但读/写失败」需要并发截短文件、
磁盘满或只读句柄，测试里没有稳定手段；实现检查由走读证明（`CACore/private/FileLoader.cpp` 的
`gcount()` 比较、`bad() || !eof()`、三处 `if (!file_dst)`），完整读数见
`openspec/changes/failure-visibility-hygiene/evidence/1.2-FileLoader红绿.txt` 第三节。

### 可复制示例（与真实用例同构）

```cpp
// 缺失路径：必抛，消息点名路径。
try {
    castl::string text = cacore::LoadStringFile("C:/no/such/file.txt");
} catch (cacore::file_error const& error) {
    // error.what() = 加载文本文件："C:/no/such/file.txt"（无法打开文件）
}

// 空文件合法：返回空，不抛。
castl::vector<uint8_t> bytes = cacore::LoadBinaryFile("C:/temp/empty.bin");
// bytes.empty() == true

// 父目录不存在：必抛，消息点名目标路径。
try {
    cacore::WriteBinaryFile("C:/no/such/dir/output.bin", data, size);
} catch (cacore::file_error const& error) {
    // error.what() = 写入二进制文件："C:/no/such/dir/output.bin"（无法创建或打开目标文件）
}
```

真实运行输出（照抄 `openspec/changes/failure-visibility-hygiene/evidence/1.2-FileLoader红绿.txt` 的绿态读数；
CoreTests 退出码 0，`PASSED` 行数 24）：

```
  [FileLoader] 抛出 file_error：加载文本文件："C:\Users\76956\AppData\Local\Temp\cascaded_fileloader_error_54424\missing_text.txt"（无法打开文件）（消息含路径=是）
  [FileLoader] 判据成立：缺失路径 LoadStringFile 抛 file_error 且消息含路径
  [FileLoader] 抛出 file_error：加载二进制文件："C:\Users\76956\AppData\Local\Temp\cascaded_fileloader_error_54424\missing_binary.bin"（无法打开文件）（消息含路径=是）
  [FileLoader] 判据成立：缺失路径 LoadBinaryFile 抛 file_error 且消息含路径
  [FileLoader] 0 字节文件：创建=是 抛出=否 返回字节数=0
  [FileLoader] 判据成立：0 字节文件 LoadBinaryFile 返回空且不抛
  [FileLoader] 抛出 file_error：写入二进制文件："C:\Users\76956\AppData\Local\Temp\cascaded_fileloader_error_54424\no_such_subdir\output.bin"（无法创建或打开目标文件）（消息含路径=是）
  [FileLoader] 判据成立：父目录不存在的 WriteBinaryFile 抛 file_error 且消息含路径
  [FileLoader] 缺失目录写之后目标文件存在=否（改前为否，静默丢写）
  [FileLoader] 判据成立：正常写入后读回的字节一致（成功路径不受影响）
FileLoader error channel test PASSED
```

### 回归时怎么把这条链路弄红

- 删掉 `LoadStringFile` 的 `is_open()` 检查 → 只有「缺失路径 LoadStringFile」一行判据不成立；
- 删掉 `LoadBinaryFile` 的 `gcount()` 检查 → 读不足分支失去检查（无独立注入读数，只能走读）；
- 删掉 `WriteBinaryFile` 的 `is_open()` 检查 → 「父目录不存在的 WriteBinaryFile」一行判据不成立；
- 三处都删 → `FileLoader error channel test FAILED`，CoreTests 退出码 1。
