# 调试与崩溃分析

本项目的崩溃分析流程、通用工具与关键技术细节。

## 1. 崩溃 dump 从哪来

- `GPUBackendTester.exe` 自带 MiniDump handler（`Test/GPUBackendTester/private/MiniDump.cpp`）：
  - 任何未处理 SEH 异常（AV、断言、非法指令等）都会触发
  - **stderr** 打印：异常码、崩溃 VA、所在模块 base + RVA、16 帧调用栈、dump 文件路径
  - 写 **`crash_YYYYMMDD_HHMMSS.dmp`**（当前工作目录，MiniDumpNormal 格式）
  - 然后 `TerminateProcess`（无对话框，headless 安全）
- 因此：**崩溃现场有两份** —— stderr 文本报告（含模块 + RVA + 栈）+ .dmp 二进制。先读 stderr 拿基本信息，再用工具符号化。

## 2. 标准流程（先读 dump 再谈决策）

1. **读 dump 到函数级根因**，再带着根因谈 scope/修复；不要把半成品根因抛给用户做决策。
2. 符号化崩溃地址 + 调用栈。
3. 验证 PDB 匹配（见 §4）。
4. 有需要再用 cdb 交叉验证。

## 3. 通用工具：`Tools/read_dump.py`

```bash
python Tools/read_dump.py <dump.dmp> [--pdb-dir <dir>...]
```

- 解析 minidump：异常流（code/tid/崩溃 VA）、模块表（97 个模块的 base/name/size）、故障线程 CONTEXT（Rip/Rsp/Rbp 等）、内存段。
- 用 dbghelp（Windows 自带，无依赖）：
  - `SymLoadModuleEx` 按 dump 内的 base 注册每个模块（ASLR 无关）
  - 符号化每个栈帧：`SymFromAddr`（函数名）+ `SymGetLineFromAddr64`（源文件:行号）
  - PDB 搜索路径**自动**取 dump 内各模块路径的目录（PDB 与 DLL 同目录）
- 调用栈：优先 `StackWalk64`；dbghelp 加载不了函数表（见 §5）时回退**栈扫描**（读 Rsp 起栈内存，找落在模块镜像区间内的返回地址），并过滤 vftable/`string`/ILT 噪音符号。
- 输出：异常信息、模块表、上下文寄存器、符号化调用栈。

已用真实 crash dump 验证能复现完整崩溃链（例：`ImageHandle::GetWindowPtr` → `VulkanGraphExecutor::PresentWindows` → `Execute` → `CompileAndExecute` → `RenderBackend_Vulkan::ExecuteGraph` → `TestTriangleWithImageBuffer`）。

## 4. 验证 PDB 与崩溃二进制匹配

符号化的前提是 PDB 是**同一构建**的产物。用 GUID 校验（不依赖工具）：

```python
dll = open(dll_path, 'rb').read()
i = dll.find(b'RSDS')                 # CodeView RSDS 记录
guid = dll[i+4:i+20]                  # 16 字节 GUID
pdb = open(pdb_path, 'rb').read()
assert guid in pdb                    # GUID 出现在 PDB 中 = 匹配
```

- DLL 与 PDB mtime 一致（ms 级）= 同一次链接产物。
- llvm-symbolizer（MSVC 自带）对 MSVC PDB 支持弱，读 dump 优先用本工具（dbghelp）或 cdb。

## 5. 关键技术细节

### minidump 结构（MiniDumpNormal，x64）
- 流类型：ThreadList=3、**ModuleList=4**、MemoryList=5、**Exception=6**、Memory64List=9。
- **模块名是 `MINIDUMP_STRING`**：`[DWORD 字节长度][UTF-16LE 内容]`，**不是** null 终止字符串。解码必须先读长度。
- `MINIDUMP_MODULE` stride：从流 `DataSize / count` 推导（本项目 = 108）。
- 异常流含故障线程 CONTEXT（AMD64 1232 字节）；MiniDumpNormal 内存段含故障线程栈。

### dbghelp 用法（ctypes）
- `WinDLL("dbghelp", use_last_error=True)`。
- **SymFromAddr / SymGetLineFromAddr64 必须设 argtypes**（`c_uint64` 地址，否则 ctypes 截断成 32 位）。
- `SYMBOL_INFO.SizeOfStruct` 必须传 **88**（MSDN 约定：真实 `sizeof(SYMBOL_INFO)`，Name[1]），非含大缓冲的实际大小；`MaxNameLen` 设缓冲容量。
- `SymLoadModuleEx` 传真实 SizeOfImage（PE OptionalHeader offset+56 读），否则地址范围注册不全 → `ERROR_MOD_NOT_FOUND(126)`。

### StackWalk64 在 minidump 分析中的坑
- 崩溃进程的模块在分析进程里未实际加载，dbghelp 的 `SymFunctionTableAccess64` 常返回 NULL（.pdata 函数表不可用）→ StackWalk64 直接 FALSE。
- 对策：回退到**栈扫描**（§3），跨版本稳定。

## 6. cdb（可选，Windows SDK Debugging Tools）

若装了 Windows SDK 的 "Debugging Tools for Windows" 组件：
```bash
cdb -z <dump.dmp> -y <pdb目录> -c "!analyze -v; q"
```
- 安装：`winsdksetup.exe /features OptionId.WindowsDesktopDebuggers /quiet /norestart`
  （`winsdksetup.exe` 从 `https://go.microsoft.com/fwlink/?linkid=2272610` 下载）
- 装后路径：`C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe`
