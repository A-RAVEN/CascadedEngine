## Context

工具的保真门把"解析不可信"判成拒收，判据逐字是
`bool untrustworthy() const { return !fatalMsg.empty() || inHeader > 0; }`（`Tools/CodeGen/CodeGenMain.cpp`）。
它带来两个后果，本次要处理的就是这两个：

- **主头自身有 1 条错误级诊断就拒收。** `Interface/RenderInterface/header/GPUGraph.h` 自身有 5 条，
  于是 `graphics_backend::ViewRectData` 无论环境多完整都进不了接入清单。
- **include 链里的错误单独不算，但攒到 clang 的 20 条上限就变成致命诊断。**
  `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` 链上已经打满，工具报
  `unresolved include (fatal): fatal error: too many errors emitted, stopping now [-ferror-limit=]`，
  于是 `GPUPipelineStateKey`、`VertexInputBindingData` 与它内部的 `InputElementDesc` 一起被拒。

今天的实测读数（探针用 `build.py` 自己推导的编译环境跑 `out/build/x64-debug/bin/CodeGen.exe`，
命令与逐字输出见本 change 的 `evidence/parse-gate-readings.md`）：

| 类型 | 头 | 退出码 | 工具给的判据 |
|---|---|---|---|
| `graphics_backend::ViewRectData` | `Interface/RenderInterface/header/GPUGraph.h` | 6 | 主头自身 5 条错误级诊断 |
| `graphics_backend::GPUPipelineStateKey` | `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` | 6 | 链上错误打满 20 条上限（致命） |
| `graphics_backend::VertexInputBindingData` | 同上 | 6 | 同上 |
| `VertexInputBindingData::InputElementDesc` | 同上 | 6 | 同上 |

`GPUGraph.h` 那 5 条逐字是：

```
3 × error: call to 'error_with_location' is ambiguous
2 × error: no member named 'second' in 'std::shared_ptr<graphics_backend::ShaderStruct>'
```

`GPUPipelineInstance.h` 链上前几条逐字是：

```
Tools/CodeGen/clang_shim\cpuid.h:15:20: error: definition of builtin function '__cpuid'
Interface/RenderInterface/header/Common.h:372:9: error: enumerator value evaluates to -1,
    which cannot be narrowed to type 'uint32_t' (aka 'unsigned int') [-Wc++11-narrowing]
out/build/x64-debug/codegen/Interface_RenderInterface/ResourceHandleKeyData.Reflection.h:15:39:
    error: constexpr variable 'arr' must be initialized by a constant expression
Interface/RenderInterface/header/ShaderBindingBuilder.h:176:3: error: call to 'error_with_location' is ambiguous
Interface/RenderInterface/header/ShaderBindingBuilder.h:177:3: error: call to 'error_with_location' is ambiguous
```

两条对照读数，说明这不是"环境缺头"那一类问题：

- `D3D12RenderBackend/private/D3D12Includes.h` 与 `D3D12RenderBackend/private/ShaderLibrary/D3D12ShaderStruct.h`
  （模块自己的头）在同一个推导环境下都是 **exit 0** —— 当年那条"`directx/d3dx12.h` 找不到"已经不成立。
- 生成物那条 `constexpr variable 'arr' ...` 是**级联**：单跑 `ResourceHandleKeyData` 与 `RectSate` 两个
  清单条目都是 exit 0、各 3 份产物，只在已经出错的那条 TU 链上才冒出来。

**约束**：引擎本体只用 MSVC 编译，上面这些诊断在 MSVC 下全都看不见（构建是通过的）。所以本次改动不许
改变任何运行时行为 —— 只在"clang 看得见、MSVC 看不见"的地方把写法改成两边都合法的形式，并且
每个键类型的哈希值都必须逐值证明不变。

## Goals / Non-Goals

**Goals:**

- 这四个键类型进 `build.py` 的 `CODEGEN_ANNOTATED`，构建期三个发射器都是退出码 0，生成物被真实编译单元
  消费，对应的手写 `custom_hash_trait` 特化被删掉。
- `GPUGraph.h` 与 `GPUPipelineInstance.h` 在推导环境下不再被保真门拒收（读数由 6 变 0）。
- 这四个类型在当前手写特化下算出的哈希值与迁移后生成物算出的值**逐个相等**（写进断言）。
- 解析链从"踩在 20 条上限边上"退下来：同一条探针命令下不再出现
  `too many errors emitted, stopping now`。

**Non-Goals:**

- 不改保真门的判法（`untrustworthy()` 一个字符不动），不设文件白名单、不抬高门槛。
- 不迁含指针成员或指针元素的那几个类型（`ImageHandle` / `BufferHandle` / `ShaderSetData` /
  `ShaderResourceSet` / `VulkanShaderResourceSet`）—— 它们的拒绝是**形状**拒绝，与本次无关。
- 不给 vendored LLVM 补 clang 资源目录（在本仓库之外）。
- 不碰 `D3D12RenderBackend` 的推导环境（今天已是 exit 0）。

## Decisions

### D1 门的判法不动，动的是被门判的东西

- **备选甲（否决）**：把 `inHeader > 0` 放宽成"允许 N 条"，或给 `GPUGraph.h` 开白名单。
  否决理由：诊断是"AST 可能被截断、字段表可能静默错"的唯一信号，放松它换来的通过是拿判据换的，
  而且归档 change 已经踩过一次同类坑（判据被改成能通过的版本）。本次的要求是"把错误清到 0"。
- **备选乙（否决）**：把这四个类型继续留作"已知边界"。
  否决理由：边界给的理由是环境性的（"解析环境过不了门"），今天已经量清楚它是**四行到几行的写法问题**；
  而且链上 20 条上限意味着别的行随时可能被连带拒掉，留着就是留着下一次。
- **决定**：门不动，把链上诊断清零。

### D2 `GPUGraph.h` 的两条 `.second`

`ForeachShaderStructs` 里逐字是
`CA_ASSERT_BREAK(shaderStruct.second != nullptr, "Shader Struct Is Null, Why!?");` 与
`shaderStructs.push_back(shaderStruct.second);`，而 `shaderStruct` 的类型是
`castl::shared_ptr<ShaderStruct>` —— 它没有 `second`。紧跟其后那段同样写法的代码已经被注释掉，
说明这是从"遍历 map 的迭代器"改写过来时的残留。MSVC 放行的原因本次**没有取到读数**
（一个说得通的假设是这个函数模板从未被实例化，但 clang 恰恰是在定义处、同样没有实例化的情况下报的这两条错 ——
同一个条件解释不了两边行为不同），所以这条只作背景，不作判据；判据是"这两条诊断有没有从工具输出里消失"。

- **决定**：先按实测列调用点（`git grep -n "ForeachShaderStructs"`）：
  - 调用点为空 → 这段是不可达代码，按"删垃圾"处理（删掉这个函数体，或按它当时的意图改成对
    `shared_ptr` 本身判空并入队，二者取"改动更小、不留误导"的那个，选择与依据写进 tasks 记录）。
  - 有调用点 → 按意图改对（判空 `shared_ptr` 本身、入队 `shared_ptr` 本身），并在调用点所在模块里
    确认语义没变。
- 判据（能失败）：改前工具的诊断列表里有这两条，改后不再有。

### D3 `Common.h` 的枚举窄化

逐字是 `eAll = ~0,`，落在 `enum class EResourceUsage : uint32_t` 里：`~0` 是 `int` 的 -1，
标准要求枚举值必须能在固定底层类型里表示，clang 按标准报错，MSVC 放行。

- **决定**：改成能在 `uint32_t` 里表示的等值写法（32 位全 1）。**值不变**是硬要求：位模式仍是 0xFFFFFFFF，
  且必须逐个消费者检查（`git grep -n "EResourceUsage::eAll"`）确认没有"依赖它是负数 / 有符号"的用法。
- 判据（能失败）：诊断列表里 `enumerator value evaluates to -1` 那条消失；消费者逐个列出并说明各自用途。

### D4 `error_with_location` 的重载歧义

`CACore/header/DebugUtils.h` 里两个重载逐字是：

```
template <typename... T> FMT_INLINE void error_with_location(std::source_location const& location, format_string<T...> fmt, T&&... args)
template <typename... T> FMT_INLINE void error_with_location(std::source_location const& location, castl::string_view const& str)
```

两者都是模板，传一个字符串字面量时两边各要一次用户定义转换，clang 判不出高下（报
`call to 'error_with_location' is ambiguous`）；MSVC 自己挑了一个，所以引擎一直编得过。
`CA_LOG_ERR` / `CA_ASSERT` / `CA_ASSERT_BREAK` 三个宏都转发到它，第二处命中
（`Interface/RenderInterface/header/ShaderBindingBuilder.h`）是同一根因。

- **决定**：消除歧义，即不再依赖"编译器替你选"。首选方案：把 `castl::string_view` 那个重载约束成
  "接受运行期字符串、不接受字符串字面量"（字面量让 `format_string` 版接手）。两条硬要求：
  ① **不许删掉任何一条调用路径** —— 运行期字符串（例如 `CA_LOG_ERR(someStdString)`）必须照样能编过；
  ② 输出行为不变（`string_view` 版内部就是转发到 `"{}"` 那条格式化路径，两条路最终同一个函数）。
- **落地前先量**：`CA_LOG_ERR` / `CA_LOG` 的调用点数量与形态（字面量 / 运行期字符串各多少），
  读数写进 tasks 记录；形态多于一种时两种都要有编译通过的证据。
- 判据（能失败）：clang 侧那 3 条 `ambiguous` 消失；MSVC 侧 `python build.py --config Debug` 通过、
  `CoreTests.exe` 退出码 0。

### D5 解析期垫片 `Tools/CodeGen/clang_shim/cpuid.h`

它是**我们仓库里的文件**（`build.py` 的 `CODEGEN_EXTRA_INCLUDES` 把它作为额外 include 根传给工具），
不是 libclang 自带的。存在的理由经核实成立：vendored 的 LLVM 只有 `bin/x64/libclang.dll`、没有 clang 的
资源头目录，而 Windows SDK 的 `DirectXMath.h` 在 clang 身份下会走
`#if (defined(__clang__) || defined(__GNUC__)) && (__x86_64__ || __i386__) && !defined(__MINGW32__)` 然后
`#include <cpuid.h>`；没有它，解析 D3D12 模块的头会以 `'cpuid.h' file not found` 失败。

今天量了三种形状（同一个探针头照抄 DirectXMath 的两处用法，`_XM_AVX2_INTRINSICS_` 开/关各跑一遍）：

| 垫片形状 | AVX2 分支关闭 | AVX2 分支打开 |
|---|---|---|
| 现状（4 个函数定义，含 `static inline void __cpuid(int out[4], int) noexcept`） | 0 条错误 | `no matching function for call to '__cpuid_count'` |
| 只留 include 守卫、什么都不定义 | 0 条错误 | `use of undeclared identifier '__cpuid_count'` |
| `__cpuid_count` 做成 6 参数宏（转发给 clang 内建 `__cpuidex`） | 0 条错误 | 0 条错误 |

DirectXMath 在 clang 下真正调用的是 6 参数形式：
`__cpuid_count(7, 0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);`。

- **决定**：垫片 MUST NOT 定义编译器自带的函数（`__cpuid` 交给 clang 的内建，DirectXMath 在
  `defined(__cpuid)` 为假时走的 `__cpuid(CPUInfo, 0)` 正好是内建的形状）；需要 GCC 形状的名字时用宏，
  且参数个数按真实调用点提供。
- **备选（不采纳，但记下来）**：给 vendored LLVM 补 clang 资源目录，垫片整个删掉 —— 最干净，但那目录
  在本仓库之外，而且本机没有第二份 `cpuid.h` 可抄（Visual Studio、`C:\Program Files\LLVM`、dotnet
  三处都搜过，零命中）。**何时可以重新考虑**：哪天 vendored LLVM 带上资源目录，或者本机装了带资源头的
  clang，就把垫片删掉改成直接用它。
- **诚实边界**：`Tools/CodeGen/clang_shim\cpuid.h:15:20: error: definition of builtin function '__cpuid'`
  这条我**只在真实链上看到**（`GPUPipelineInstance.h` 探针），四种隔离上下文都没复现：
  先 `#include <intrin.h>`、先 `#include <Windows.h>`、先 `#include <DirectXMath.h>`、以及"先调用
  `__cpuid` 再 include 垫片"。**这四种上下文已经写进 `evidence/probe_shim_shape.py` 的第二部分**
  （原先是口头声称、没有脚本支撑），今天重跑四种都是 exit 0、都不出现该诊断。
  所以任务里要求：先把它固定在一条可重跑的命令上（红态），改完再跑同一条命令 —— 若那条错误消失即绿；
  若仍在，按实测继续定位，**不许声称已修好**；若构造不出独立红态，按项目规则在记录里写明
  「未能证明此判据可失败」。

### D6 四个类型进清单后的哈希值守卫

手写特化与生成代码的口径**声称**一致（那句话逐字写在两处手写特化的注释里 ——
`Interface/RenderInterface/header/GPUGraph.h` 的 `// 口径与生成代码一致：逐个字段算，…` 与
`D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` 的同名注释；**不在 `build.py` 里**），
逐行比对两个实现：

- 手写：`hasher.hash(obj.x)`；生成：`cgHashContainerValue(h, obj.x)`。
  `CACore/header/CodeGenHash.h` 里 `cgHashContainerValue` 对非浮点、非容器、无特化的类型落到 `h.hash(e);`
  —— 与手写调用**同一条路**。
- **差异点一（浮点）**：手写的 `hasher.hash(float)` 走"平凡可拷贝 → 按字节"，**不归一化**；
  而 `cgHashContainerValue` 对 `float` / `double` 会先把 `-0.0` 归一成 `+0.0`、把 NaN 归一成一个稳定值。
  含浮点成员的类型迁移后哈希值**会变**。四个目标类型都不含浮点成员（任务 4.2 要逐个核实）。
- **差异点二（定长数组成员）**：生成侧 `cgHashContainerValue` 对 `std::is_array_v` 为真的成员走
  "先写元素个数、再逐个元素"，而手写调用 `hasher.hash(数组成员)` 走"平凡可拷贝 → 整块按字节"。
  四个目标类型都不含定长数组成员（同上，任务 4.2 核实）。
- **一条不许用的判据**：`cacore::dispatch_branch_unchanged<T>()`（`CACore/header/HashKeys.h`）对
  "既平凡可拷贝、又带自定义特化"的类型**恒为假** —— 它的两条式子逐字是
  `constexpr int  before  = trivial ? 1 : (custom ? 2 : 0);` 与
  `constexpr int  after   = custom ? 2 : (trivial ? 1 : 0);`。四个类型里 `ViewRectData`（四个 `int` 加
  默认三向比较）就属于这一类 —— 所以**不许**给它加那条断言（加了编不过），也不许拿它当"哈希已生效"的证据；
  值相等只能靠逐值对拍证明。

- **决定**：迁移前先对这四个类型的实例，**经既有入口**（`cacore::hash<T>` 或 `HashObj<T>::GetHash()`，
  不是直接调特化）测出当前哈希值并落档（期望值先跑出来再写，不许手算）；迁移后经同一入口再取一次，必须逐个相等。
  **含浮点成员或定长数组成员的例外必须单独处理**：先核对这四个类型的成员类型，若命中就在记录里写明
  "值会变"并重新评估该类型是否迁移，不许默默换掉。
- 判据（能失败）：对同一个对象造两份、只把**填充字节**改成不同值，经既有入口取到的哈希必须相等，
  而"整块按字节"口径下的两个哈希必须不同 —— 后者是反向对照，用来证明这条守卫不是恒真
  （现成样板：`Test/CoreTests/GeneratedHashTest.cpp` 里的 `hashIgnoresPadding` 与 `rawBytesDiffer`）。
  ⚠ 拿含 `-0.0f` 的对象去证明"浮点会归一化"**不算**这条判据 —— 那是差异点一的读数，与这四个类型无关。

### D7 落盘路径不受影响

这四个类型都不是 `TResource` 的载荷：`ViewRectData` 只当 `HashObj<ViewRectData>` 的键，
`GPUPipelineStateKey` / `VertexInputBindingData` 只在 D3D12 的内存字典里当键。**这是要先核实的断言，不是结论**：
任务里要求逐个确认它们不出现在任何已迁移类型的成员链上（出现在成员链上就会进落盘字节）。

### D8 "链上错误清零"怎么量（原先的判据量不出来）

保真门只判"主头自身有错误"或"有致命诊断"，include 链里的错误单独不触发拒收，**而且诊断只在门被触发时
才打印**。于是"退出码 0"分不清"链上真干净"与"链上还剩几条没打印"；`ShaderLibrary` / `RectSate` 这类
今天已经在清单里的行就是这个状态（不传开关 exit 0、stderr 空，传 `-ferror-limit=1` 就是 exit 6 并打印
`Common.h:372` 那条窄化）。

- **决定**：链上是否清零，用 `evidence/probe_chain_clean.py` 量 —— 它对同一个头跑三次调用时加上
  `-ferror-limit=1`，把链上任何一条错误级诊断升级成致命诊断，工具就会把错误原文打印出来。
  判据：**不出现致命诊断、且不打印任何错误级诊断**才算清零；出现任何一条都要逐条处置（记原文 + 处置依据）。
  "错误总数压到上限以下"**不算**清零。
- 配套红线：推导环境与 `CODEGEN_EXTRA_INCLUDES` 里 MUST NOT 出现 `-ferror-limit`、`-w`、`-Wno-error`
  这类把上限抬高或把错误降级的开关（工具自己不传这些开关，实测 `Tools/` 下零命中）。

### D9 解析环境额外加一个宏：`-D_CRT_USE_BUILTIN_OFFSETOF`

清零过程中发现的最后一处错误不在引擎头里，而在**生成物自己**身上：`out/build/x64-debug/codegen/**/*.Reflection.h`
里的 `static constexpr CodeGenFieldInfo arr[…]` 被 clang 判为"不是常量表达式"。隔离到一行之后根因是 `offsetof`：
UCRT 的 `stddef.h` 逐字写着

```
#if defined _MSC_VER && !defined _CRT_USE_BUILTIN_OFFSETOF
#define offsetof(s,m) ((::size_t)&reinterpret_cast<char const volatile&>((((s*)0)->m)))
#else
#define offsetof(s,m) __builtin_offsetof(s,m)
#endif
```

—— clang 在 MSVC 目标上**也定义 `_MSC_VER`**，于是拿到的是空指针解引用那一版。MSVC 自己会把这个模式折叠成常量，
clang 不会（它只在自己的 `stddef.h` 下才用 `__builtin_offsetof`）。实测：`static constexpr std::size_t offA = offsetof(WithInts, a);`
报 `constexpr variable 'offA' must be initialized by a constant expression`、工具 `exit=6`；加宏后同一个探针 `exit=0`。

- **决定**：在 `build.py` 的 `CODEGEN_EXTRA_DEFINES` 里加 `-D_CRT_USE_BUILTIN_OFFSETOF`，只作用于工具的解析命令行。
  **产物文本一个字符都不改**（仍是 `offsetof(...)`），所以 MSVC 侧编译这些产物时走的还是它自己的宏 —— 这条改动
  不可能影响运行时行为。
- **备选（否决）**：让发射器写 `__builtin_offsetof`，并在 `CodeGenMeta.h` 里给 MSVC 兜一个 `#define`。
  否决理由：那要动生成物文本 + 运行时头 + README 里的产物样例，收益与"加一个解析期宏"完全相同，风险面更大
  （`__builtin_offsetof` 在 MSVC 上是否可用还得另测）。
- 配套红线：这个宏只许出现在解析环境里；引擎的编译宏集合与产物的文本 MUST NOT 因此改变（判据：`git diff` 里
  `out/build/` 之外的产物消费者一行未动 + 全量构建通过）。

### D10 哈希特化必须在"会哈希它的每个编译单元"里可见（ODR 事故）

任务 4.1 的读数刚加进 `Test/CoreTests/GeneratedHashTest.cpp` 时，只 include 了两个类型定义头，没有 include
哈希链上那些类型的生成物。后果不是编译错误，而是**整个二进制的哈希口径被换掉**：`PipelineStateObjectTest.cpp`
里那条"哈希忽略填充字节"的判据由 1 掉成 0（`CPipelineStateObject hash ignores padding = 0 … HashObj equal = 0`），
`CoreTests.exe` 退出码由 0 变 1。

机制：`hasher::hash<T>` 是模板。看不到 `custom_hash_trait<CPipelineStateObject>` 的编译单元里，这个宿主是
"平凡可拷贝"→ 走整块按字节；看到的单元里走逐字段。两个实例都是弱符号，链接器合并之后整个二进制只剩一个，
于是**任意一个漏了 include 的编译单元都能把全体的哈希口径改掉**（实测 `GPUPipelineStateKey` 的读数也变了：
5365906964468935195 → 17773842414398720855）。

- **决定**：任何要哈希某个类型的编译单元，MUST 把该类型哈希链上用到的生成物自己 include 一遍 ——
  这是仓库里已有的做法（`D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h` 末尾那段生成物 include，
  注释逐字写着"消费方自己记一遍 —— `TResource<T>` 的读写与 `HashObj<T>` 的哈希在**实例化**时就需要看到这些类型的"）。
  本次新增的读数与断言（4.1、4.3~4.6）MUST 照此办，落点处要写清它 include 了哪几份生成物。
- **判据（能失败）**：漏 include 时，`PipelineStateObjectTest.cpp` 的
  `CPipelineStateObject hash ignores padding` 会变成 0 且 `CoreTests.exe` 退出码变 1 —— 这条已经在
  `evidence/parse-gate-readings.md` 第十二节留了改前/改后两次读数与两次对照构建。
- **与"既有调用点断言"的关系**：基线规格要求"判 `生效` 的地方 MUST 是既有调用点"。这条事故说明**只看编译期
  `has_custom_hash_func` 断言是不够的**：`GeneratedHashTest.cpp` 里那份断言当时也成立，问题出在运行时选中的是
  哪一份实例。所以 4.3~4.6 的判据 MUST 同时包含"经既有入口取到的值相等"这一条运行时读数。

## Risks / Trade-offs

- [`__cpuid` 那条错误的触发条件没查清] → 四种隔离上下文已经写进探针（都是 exit 0、都不出现该诊断），
  真实链上确实出现；改完若那条消失即绿，若不消失就继续定位，不拿"看起来应该好了"当验收。
- [改 `DebugUtils.h` 会碰到全局日志宏，波及面大] → 只消除歧义、不删调用路径；落地前先量调用点数量与形态，
  两种形态各留一条"工具输出里 ambiguous 条数为 0"的证据（"编译通过"不算 —— 那两行在 MSVC 下改动前也编得过）。
- [清掉今天看见的 5 条后，链上还有别的错误（工具只打印前 5 条）] → 按 D8 用 `-ferror-limit=1` 量，
  把剩余错误逐条列出并处理；没清零就不算完成。
- [哈希值守卫把期望值钉死，将来改类型定义会连带红] → 这是有意为之：改了字段就该重新量。
  任务 4.2 里写明"改这四个类型的成员时必须重测这组期望值"。
- [清单 72 → 76 行，构建期解析变重] → 记录读数：改完重测全量构建耗时并同步 `CLAUDE.md` 里那串时间，
  不设任何配额。
- [生成物进清单后，`D3D12RenderBackend` 的私有头里出现 `#include "…Hash.h"`] → 该模块的
  `CMakeLists.txt` 已经把 `${CMAKE_BINARY_DIR}/codegen/D3D12RenderBackend` 加进私有 include，
  无需改构建；若实测缺，才补。
- [四个类型的断言落点不能都写在 Interface 的断言文件里] → `Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp`
  只挂了 Interface 自己的头，看不到 D3D12 的私有头；另外三个类型的断言 MUST 落在 D3D12 模块自己的
  编译单元里（该模块已有 `ShaderLibraryCodeGenAssertions.cpp` 这种落点可参照）。

## 已知边界

本节记录**本次明确不迁**的类型与它们的依据（任务 4.7 若撞上形状拒绝，也把读数写在这里）。

| 类型 | 依据（可核） | 何时可以重新考虑 |
|---|---|---|
| `graphics_backend::ImageHandle` / `BufferHandle` | 各含一个 `castl::shared_ptr` 成员（`m_ExternalManagedTexture` / `m_ExternalManagedBuffer`），工具的指针成员拒绝（退出码 15）；它们是哈希键、不是落盘载荷，按"键类型 + 手写特化"处理。红态读数在归档 change 的 design「已知边界」一节 | 某个句柄真的要落盘时：把可落盘部分提成值语义 + 旁表索引，再加标注进清单 |
| `graphics_backend::ShaderSetData` | 含非拥有裸指针成员 `reflectionData`（退出码 15）；它只是 `GetShaderCodes()` 的临时返回值，不是任何已标注类型的成员。读数同样在归档 change 的 design | 它真的要落盘时 |
| `graphics_backend::ShaderResourceSet`（D3D12） | `resourceDic` 是"名字 → 结构体指针"的映射，容器元素是指针（形状拒绝）；手写特化的理由逐字写在 `D3D12RenderBackend/private/GPUGraph/GPUResourceBindingInstance.h` 的注释里 | 该映射改成值语义（或旁表）时 |
| `graphics_backend::VulkanShaderResourceSet` | 哈希直接取它缓存好的 `hash` 成员（遍历那个无序映射会因桶布局不同算出不同值），理由逐字写在 `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h` 的注释里 | 该缓存字段取消时 |
| `ViewRectData` / `GPUPipelineStateKey` / `VertexInputBindingData` / `InputElementDesc` | **不在本表**：本次要迁的就是它们（原先的排除理由"解析环境过不了门"随本次改动失效） | — |

## Migration Plan

1. 把探针固化进本 change 的 `evidence/`（可重跑），先留红态读数：四个类型 exit 6、两个头各自的诊断原文。
2. 引擎侧三处清零：`GPUGraph.h` 的 `.second`、`Common.h` 的 `eAll`、`DebugUtils.h` 的歧义。
   每处改完立刻跑同一条探针，看对应的那几条是否消失（不许一次改三处再一起看）。
3. 垫片：先固定 `__cpuid` 那条的红态，再改形状，再跑同一条命令。
4. 四个类型进清单：先测哈希值 → 加清单行 + 头里 include 生成物 + 断言 → 删手写特化 → 逐个跑构建。
5. 收尾：全量构建、`CoreTests.exe`、两个后端 `GPUBackendTester.exe --headless 10`、扫描器、
   `openspec validate`、文档（README 补垫片一节、`CLAUDE.md` 时间串）。
6. 回滚：本次全是工作区改动，回退就是在提交前 `git checkout -- <文件>`（不改历史、不用 reset）。

## Open Questions

- `ForeachShaderStructs` 有没有调用点（决定"删掉"还是"改对"）—— 任务里第一件事就是量。
- `error_with_location` 的两个重载各自的调用形态分布（字面量 / 运行期字符串）—— 决定约束怎么写。
- `__cpuid` 那条诊断的触发条件 —— 若任务里仍复现不出独立红态，就按 D5 的诚实边界处理。
