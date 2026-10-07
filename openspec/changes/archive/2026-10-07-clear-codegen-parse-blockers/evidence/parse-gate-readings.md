# 解析门读数（本 change 动工之前）

**读数取自**：提交 `32341b9`（工作区当时只有本 change 的未跟踪目录），日期 **2026-10-05**；
Windows SDK 取 `10.0.26100.0`（同机的 `10.0.19041.0` 里 `DirectXMath.h` 的 clang 分支写法不同，
换一份 SDK 时下面的垫片读数要重新量）。工具是 `out/build/x64-debug/bin/CodeGen.exe`，
解析环境一律由 `build.py` 从 `compile_commands.json` 推导。

本目录四份探针都可原样重跑，命令写在每一节里。它们只往 `%TEMP%` 写产物，并且脚本开头关掉了字节码缓存写入，
所以不会在仓库里留下 `__pycache__`。

## 一、四个目标类型：全部被拒

```
python openspec/changes/clear-codegen-parse-blockers/evidence/probe_parse_gate.py
```

逐字读数（节选）：

```
  ViewRectData  exit=6  主头诊断=5  致命=False  产物=0
    头: Interface/RenderInterface/header/GPUGraph.h
    |   5 error-level diagnostic(s) inside the parsed header itself
  GPUPipelineStateKey  exit=6  主头诊断=0  致命=True  产物=0
    头: D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h
    |   unresolved include (fatal): fatal error: too many errors emitted, stopping now [-ferror-limit=]
  VertexInputBindingData  exit=6  主头诊断=0  致命=True  产物=0
  InputElementDesc  exit=6  主头诊断=0  致命=True  产物=0
```

| 类型 | 头 | 退出码 | 工具给的判据 |
|---|---|---|---|
| `graphics_backend::ViewRectData` | `Interface/RenderInterface/header/GPUGraph.h` | 6 | 主头自身 5 条错误级诊断 |
| `graphics_backend::GPUPipelineStateKey` | `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` | 6 | 链上错误打满 clang 的错误上限（致命） |
| `graphics_backend::VertexInputBindingData` | 同上 | 6 | 同上 |
| `VertexInputBindingData::InputElementDesc` | 同上 | 6 | 同上 |

## 二、`GPUGraph.h` 自身那 5 条（逐字）

工具的"空标注"那条提前返回只打印**条数**，所以要看内容得给目标类型补一行标记再解析 ——
下面的脚本把文件拷进 `%TEMP%`、插两行（`#include <CodeGenMeta.h>` 与 `CA_REFLECT(serializable)`），
**不改仓库里的文件**：

```
python openspec/changes/clear-codegen-parse-blockers/evidence/probe_header_diagnostics.py \
    Interface/RenderInterface/header/GPUGraph.h ViewRectData Interface/RenderInterface "struct ViewRectData"
```

```
退出码：6
CodeGen: refusing to extract from '<tmp>\GPUGraph.h' — the compile environment fed to this tool is incomplete, ...
  5 error-level diagnostic(s) inside the parsed header itself:
    <tmp>\GPUGraph.h:464:4: error: call to 'error_with_location' is ambiguous
    <tmp>\GPUGraph.h:690:33: error: no member named 'second' in 'std::shared_ptr<graphics_backend::ShaderStruct>'
    <tmp>\GPUGraph.h:690:4: error: call to 'error_with_location' is ambiguous
    <tmp>\GPUGraph.h:691:41: error: no member named 'second' in 'std::shared_ptr<graphics_backend::ShaderStruct>'
    <tmp>\GPUGraph.h:706:4: error: call to 'error_with_location' is ambiguous
```

正好 5 行 = 3 条 `ambiguous` + 2 条 `.second`（工具每类诊断最多收 5 条，所以这里不会是别的条数）。

⚠ 副本比源文件多 2 行（插了那两行），**真实行号 = 副本行号 − 2**：462、688、688、689、704。
对照源文件：462 行是 `CA_ASSERT(valid, "Invalid Compute Dispatch!");`，688/689 两行是
`ForeachShaderStructs` 里对 `castl::shared_ptr` 取 `.second` 的那两处，704 行是同一函数里的
`CA_ASSERT_BREAK(pStruct != nullptr, "Shader Struct Is Null, Why!?");`。

## 三、D3D12 那条链上的诊断（逐字）

```
python openspec/changes/clear-codegen-parse-blockers/evidence/probe_header_diagnostics.py \
    D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h GPUPipelineStateKey D3D12RenderBackend \
    "struct GPUPipelineStateKey"
```

```
退出码：6
  unresolved include (fatal): fatal error: too many errors emitted, stopping now [-ferror-limit=]
  前几条错误（不在被解析的头里，多半来自它 include 的文件）:
    Tools/CodeGen/clang_shim\cpuid.h:15:20: error: definition of builtin function '__cpuid'
    Interface/RenderInterface/header/Common.h:372:9: error: enumerator value evaluates to -1,
        which cannot be narrowed to type 'uint32_t' (aka 'unsigned int') [-Wc++11-narrowing]
    out/build/x64-debug/codegen/Interface_RenderInterface/ResourceHandleKeyData.Reflection.h:15:39:
        error: constexpr variable 'arr' must be initialized by a constant expression
    Interface/RenderInterface/header/ShaderBindingBuilder.h:176:3: error: call to 'error_with_location' is ambiguous
    Interface/RenderInterface/header/ShaderBindingBuilder.h:177:3: error: call to 'error_with_location' is ambiguous
  degraded members of 'GPUPipelineStateKey' (reported offset 0 although not the first member):
    m_PipelineStates, m_InputAssemblyStates, m_AttachmentFormats, m_DepthFormat, m_VertexInputBindingData
```

⚠ **这只是前 5 条**：工具的诊断列表被截到 5 条（源码里两处 `size() < 5`），所以"链上到底还有几条"
必须靠下一节那把尺子量，不能数这里。

## 四、链上错误有没有清零：`-ferror-limit=1`

为什么需要这把尺子：保真门判据逐字是 `bool untrustworthy() const { return !fatalMsg.empty() || inHeader > 0; }`
—— include 链里的错误**单独不触发拒收**，而且诊断只在门被触发时才打印。所以"退出码 0"分不清
"链上真干净"与"链上还剩几条没打印"。把 clang 的错误上限压到 1，链上任何一条错误都会升级成致命诊断，
工具就会把它打印出来。

```
python openspec/changes/clear-codegen-parse-blockers/evidence/probe_chain_clean.py
```

实测（2026-10-05，四个头全部脏）：

| 头 | 不传开关 | `-ferror-limit=1` |
|---|---|---|
| `Interface/RenderInterface/header/GPUGraph.h`（`ViewRectData`） | exit 6（主头 5 条） | exit 6 + 致命 |
| `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h`（`GPUPipelineStateKey`） | exit 6 + 致命 | exit 6 + 致命 |
| `Interface/RenderInterface/header/CPipelineStateObject.h`（`RectSate`，**已在清单里**） | **exit 0、stderr 空** | **exit 6**：`Common.h:372:9: error: enumerator value evaluates to -1 …` |
| `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h`（`ShaderLibrary`，**已在清单里**） | **exit 0、stderr 空** | **exit 6**：同一条 `Common.h:372` |

也就是说：连今天构建得通、产物正常的行，链上也不是干净的 —— 它们只是没攒到上限。

## 五、对照：环境本身是好的

同一条 `probe_parse_gate.py` 里的三行对照读数：

| 对照 | 退出码 | 说明 |
|---|---|---|
| `ResourceHandleKeyData`（已在接入清单里，头是 `ShaderResourceHandle.h`） | 0 | 3 份产物，链上那些诊断单独不触发拒收 |
| `RectSate`（已在接入清单里，头是 `CPipelineStateObject.h`，它也 include 了 `Common.h`） | 0 | 同上 —— 证明 `Common.h:372` 那条是"链上错误"，不是"一 include 就拒" |
| `D3D12RenderBackend/private/D3D12Includes.h`（模块自己的头） | 0 | 当年那条 `directx/d3dx12.h` 找不到已经不成立；注意它没有标注类型、不产出生成物，所以它只证明"环境能解析"，不证明"链上干净" |

## 六、解析期垫片

```
python openspec/changes/clear-codegen-parse-blockers/evidence/probe_shim_shape.py
```

第一部分：三种形状 × `_XM_AVX2_INTRINSICS_` 开/关（探针头照抄 Windows SDK `DirectXMathMisc.inl` 里
`XMVerifyCPUSupport` 的两处调用形状，标记带 `serializable`，所以"解析干净"的读数就是退出码 0）：

| 垫片写法 | AVX2 关 | AVX2 开 |
|---|---|---|
| 现状（4 个函数定义，含 `__cpuid`） | exit 0 | exit 6：`no matching function for call to '__cpuid_count'` |
| 什么都不定义（只留 include 守卫） | exit 0 | exit 6：`use of undeclared identifier '__cpuid_count'` |
| `__cpuid_count` 做成 6 参数宏（转发给 clang 内建 `__cpuidex`） | exit 0 | **exit 0** |

DirectXMath 在 clang 下真正调用的是 6 参数形式：
`__cpuid_count(7, 0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);`

第二部分：四种隔离上下文（都用现状垫片、都不开 AVX2），**四种都是 exit 0、都不出现
`definition of builtin function`**：

| 上下文 | 读数 |
|---|---|
| 先 `#include <intrin.h>` 再 include 垫片 | exit 0，无该诊断 |
| 先 `#include <Windows.h>` 再 include 垫片 | exit 0，无该诊断 |
| 先 `#include <DirectXMath.h>`（真实链条） | exit 0，无该诊断 |
| 先调用 `__cpuid` 再 include 垫片 | exit 0，无该诊断 |

结论：`definition of builtin function '__cpuid'` **只在真实 D3D12 链上出现**（第三节逐字），
四种隔离上下文都复现不出，触发条件未查明。任务 3.1 因此要求先把它固定在一条可重跑的命令上；
若仍复现不出独立红态，MUST 逐字写明「未能证明此判据可失败」并列出试过的上下文，不许默认它有效。

## 七、门的判据（机制）

`Tools/CodeGen/CodeGenMain.cpp` 里逐字：

```
bool untrustworthy() const { return !fatalMsg.empty() || inHeader > 0; }
```

- 主头**自身**有 1 条错误级诊断 ⇒ 拒收（`ViewRectData` 就是这一类）。
- include 链里的错误**单独不算**（第五节三个对照就是证据），但攒到 clang 的错误上限时变成致命诊断
  （`too many errors emitted, stopping now`）⇒ 拒收（另外三个类型是这一类）。
- 那个上限是 clang 的默认值：官方手册逐字 `Stop emitting diagnostics after 123 errors have been produced.
  The default is 20, and the error limit can be disabled with -ferror-limit=0.`
  （<https://clang.llvm.org/docs/UsersManual.html>）。工具自己不传这个开关，也没有任何地方规定
  不许它从推导环境里进来 —— 所以任务里补了一条：环境里不许出现抬高上限的开关。

## 八、绿态判据不许只看退出码

工具在"这个头里一条标注都没有"时打印
`CodeGen: no annotated types (empty whitelist) in '…'; nothing to generate` 并 **exit 0，一份产物都不写**。
第五节第三个对照（`D3D12Probe`，`exit=0 产物=0`）就是这条通路。
所以 `probe_parse_gate.py --expect green` 的判据是**退出码 0 且写出 3 份产物、且不出现 `no annotated types`**；
只看退出码会把"标注丢了"判成通过（`build.py` 里本来就有这条判据：
`CodeGen '<类型>' --emit-all: 工具退出码 0，却没有写出这些产物：… —— 多半是这个类型的标注被删了`）。

## 九、apply 第 1 轮：红态复核（2026-10-05，HEAD = `32341b9`）

工作区当时只有本 change 的未跟踪目录。原样跑两条命令：

```
python -B openspec/changes/clear-codegen-parse-blockers/evidence/probe_parse_gate.py
python -B openspec/changes/clear-codegen-parse-blockers/evidence/probe_chain_clean.py
```

原始结论行（逐字）：

```
期望=red；不符合期望的：（无）；跑了 4/4 个目标、3/3 个对照；跳过 0 个
期望=dirty；链上仍有错误的：['ViewRectData', 'GPUPipelineStateKey', 'RectSate', 'ShaderLibrary']
ViewRectData  (Interface/RenderInterface/header/GPUGraph.h)  exit=6  错误级诊断行数=1
GPUPipelineStateKey  (D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h)  exit=6  错误级诊断行数=1
RectSate  (Interface/RenderInterface/header/CPipelineStateObject.h)  exit=6  错误级诊断行数=3
ShaderLibrary  (D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h)  exit=6  错误级诊断行数=3
```

与前面各节一致：四个目标类型仍被拒；两个**已经在清单里**的行在默认开关下是 exit 0，
加上 `-ferror-limit=1` 之后同样是脏的（`Common.h:372` 那条）。

## 十、apply 第 1 轮：任务 1.2 的三组调用点读数（2026-10-05，HEAD = `32341b9`）

**① `git grep -n "ForeachShaderStructs"`** —— 只有定义、**零调用点**：

```
Interface/RenderInterface/header/GPUGraph.h:683:	static void ForeachShaderStructs(TSSRange const& inShaderStructRange, castl::function<void(TSS const&)> callback)
```

**② `git grep -n "EResourceUsage::eAll"`** —— 只有一处消费者：

```
D3D12RenderBackend/private/ResourceManagment/GPUResourceStates.h:477:			EResourceUsageFlags directQueueUsages = EResourceUsage::eAll;
```

（`Interface/RenderInterface/header/Common.h:427` 的 `eAll,` 与
`VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h:38` 的 `ECullMode::eAll` 都属于**另一个枚举**
`ECullMode`，不是 `EResourceUsage`。）该消费者的用法是 `ResourceUsageCompatible`（同文件 473-480 行）：
在 direct 队列上取"全部用法"再 `HasAll(resourceUsages)` —— 所以 `eAll` 的语义就是"32 位全 1"。

**③ 三个日志/断言宏的调用形态**（`CA_LOG_ERR` / `CA_LOG_ERR_BREAK` / `CA_ASSERT` / `CA_ASSERT_BREAK`，
按顶层逗号切参数、`git ls-files` 的 `.h`/`.cpp` 全量）：

| 形态 | 处数 |
|---|---|
| 传字符串字面量 | **333** |
| 传运行期字符串 / 表达式 | **13**（其中 4 处是宏定义体自身） |

非字面量的调用点分布在 6 个文件，**这些路径必须保留**（改 2.2 时的硬约束）：

- `ShaderCompilerSlang/private/Compiler.cpp`：3 处 `CA_LOG_ERR((const char*)diagnostics->getBufferPointer())`
- `ShaderProcessor/Main.cpp`：2 处 `CA_LOG_ERR(pErrors->GetStringPointer())` / `CA_LOG_ERR(pPreprocessed->GetStringPointer())`
- `Interface/RenderInterface/header/ShaderArgList.h`：1 处字符串拼接表达式
- `TimerSystem_Impl/private/TimperSystem_Impl.cpp`：1 处字符串拼接表达式
- `CACore/header/DebugUtils.h` 与 `VulkanRenderBackendNew/private/Utils/VulkanDebug.h`：宏定义体各 2 处

## 十一、apply 第 1 轮：链上最后一处错误的根因（生成物里的 `offsetof`）

清零过程中，`probe_chain_clean.py` 一度只剩一处错误：

```
out/build/x64-debug/codegen/Interface_RenderInterface/ResourceHandleKeyData.Reflection.h:15:39:
    error: constexpr variable 'arr' must be initialized by a constant expression
```

最小对照（同一环境逐项隔离）：`static constexpr std::size_t offA = offsetof(WithInts, a);` 与带 `offsetof` 的
`CodeGenFieldInfo` **都报** `constexpr variable … must be initialized by a constant expression`；
不带 `offsetof` 的单条结构体、数组、普通聚合数组三种写法**都是干净的**。所以问题在 `offsetof` 本身。

根因逐字在 UCRT 的 `stddef.h`（`C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt\stddef.h`）：

```
#if defined _MSC_VER && !defined _CRT_USE_BUILTIN_OFFSETOF
#define offsetof(s,m) ((::size_t)&reinterpret_cast<char const volatile&>((((s*)0)->m)))
#else
#define offsetof(s,m) __builtin_offsetof(s,m)
#endif
```

clang 在 MSVC 目标上**也定义 `_MSC_VER`**，于是拿到空指针解引用那一版；MSVC 自己会折叠它，clang 不会。
读数：加宏前 `exit=6`（`offA` 与 `oneWithOffset` 两条），加 `-D_CRT_USE_BUILTIN_OFFSETOF` 后 `exit=0`。

处置：宏加在 `build.py` 的 `CODEGEN_EXTRA_DEFINES`，只作用于工具的解析命令行；产物文本仍是 `offsetof`。
改完的链上读数（`probe_chain_clean.py --expect clean`）：

```
ViewRectData  (Interface/RenderInterface/header/GPUGraph.h)  exit=0  错误级诊断行数=0
GPUPipelineStateKey  (D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h)  exit=0  错误级诊断行数=0
RectSate  (Interface/RenderInterface/header/CPipelineStateObject.h)  exit=0  错误级诊断行数=0
ShaderLibrary  (D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h)  exit=0  错误级诊断行数=0
期望=clean；链上仍有错误的：（无）
```

## 十二、apply 第 1 轮：四个键类型的哈希期望值（任务 4.1），以及一次 ODR 事故

读数取自 `Test/CoreTests/GeneratedHashTest.cpp` 里新加的那段（**经既有入口** `cacore::hash<T>`，
不是直接调特化），提交 `32341b9` + 本轮改动、`CoreTests.exe` 退出码 **0**：

```
CodeGen key hash readings (ViewRectData / InputElementDesc / VertexInputBindingData / GPUPipelineStateKey) = 9566659391000707361 / 7588715830773829420 / 17516807725481476295 / 17773842414398720855
```

| 类型 | 迁移前的哈希值（期望值） |
|---|---|
| `graphics_backend::ViewRectData` | 9566659391000707361 |
| `VertexInputBindingData::InputElementDesc` | 7588715830773829420 |
| `graphics_backend::VertexInputBindingData` | 17516807725481476295 |
| `graphics_backend::GPUPipelineStateKey` | 17773842414398720855 |

**事故（已修，留档）**：这段读数第一次加进 `GeneratedHashTest.cpp` 时只 include 了两个类型定义头，
没 include 哈希链上那些类型的生成物。后果不是编译错误，而是**整个二进制的哈希口径被换掉**：

```
改之前（正确）：CPipelineStateObject hash ignores padding = 1 (fields equal = 1, overall bytes differ = 1, HashObj equal = 1, control differs = 1)
只加测量块：    CPipelineStateObject hash ignores padding = 0 (fields equal = 1, overall bytes differ = 1, HashObj equal = 0, control differs = 1)
```

机制：`hasher::hash<CPipelineStateObject>` 是个模板，在**看不到生成特化**的编译单元里会走"平凡可拷贝 → 整块按字节"，
在看到的单元里走"自定义特化 → 逐字段"；两个实例都是弱符号，链接器合并后整个二进制只剩一个 ——
`PipelineStateObjectTest.cpp` 于是跑了整块口径那一路，填充字节开始参与哈希。
`GPUPipelineStateKey` 的读数也跟着变了（修之前 5365906964468935195 → 修之后 17773842414398720855）。
修法：那段测量所在的编译单元把哈希链上用到的生成物自己 include 一遍
（`CPipelineStateObject.Hash.h` / `RasterizerStates.Hash.h` / `DepthStencilStates.Hash.h` / `StencilStates.Hash.h` /
`SingleColorAttachmentBlendStates.Hash.h` / `ColorAttachmentsBlendStates.Hash.h` / `ShaderInfo.Hash.h` /
`InputAssemblyStates.Hash.h`），与 `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h` 末尾那段同一个道理。
修完 `CoreTests.exe` 退出码回到 **0**、那条判据回到 **1**。

定位过程（两次对照构建）：① `git stash` 掉本轮全部改动后重建 → 判据 **1**、退出码 0；
② 只回退 `GeneratedHashTest.cpp`（保留其余改动）→ 判据 **1**、退出码 0 ⇒ 触发者是那段测量本身，不是别的改动。
另外单独验过 `-D_CRT_USE_BUILTIN_OFFSETOF` **不改变生成物文本**（同一批类型带/不带该宏各生成一次，三份产物逐字节相同）。
