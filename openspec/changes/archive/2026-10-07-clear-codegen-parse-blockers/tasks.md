# 任务清单

每一步的判据都要能在"问题还在"的状态下变红。构建一律用 `python build.py`（本项目唯一合法的构建入口），
工具一律是 `out/build/x64-debug/bin/CodeGen.exe`，解析环境一律由 `build.py` 自己推导（不许手写 include 清单）。

## 1. 红态固化（动任何代码之前）

- [x] 1.1 复核本 change 的红态记录：在 `32341b9`（记录里写的那个代码状态）上原样跑
      `python openspec/changes/clear-codegen-parse-blockers/evidence/probe_parse_gate.py`，
      确认四个类型的退出码读数是 **6**（`ViewRectData` / `GPUPipelineStateKey` / `VertexInputBindingData` /
      `InputElementDesc`），且 `GPUGraph.h` 报"主头自身 5 条错误级诊断"、`GPUPipelineInstance.h` 报
      `too many errors emitted, stopping now`；再跑 `evidence/probe_chain_clean.py` 记下四个头在
      `-ferror-limit=1` 下的读数（改之前四个全脏）。把这两次的命令与原始输出贴进
      `evidence/parse-gate-readings.md` 末尾。
      **怎么算做完**：两份探针都跑通、读数与记录一致；不一致就先查清为什么（环境变了 / 头改了 / 提交不同），
      不许带着不一致往下走。⚠ 判定口径：`--expect green` 要求**退出码 0 且写出 3 份产物**，
      因为工具在"这个头里一条标注都没有"时也是 exit 0 且零产物。
- [x] 1.2 量三组调用点，读数写进 `evidence/parse-gate-readings.md`：
      ① `git grep -n "ForeachShaderStructs"`（决定 2.1 是"删死代码"还是"改对"）；
      ② `git grep -n "EResourceUsage::eAll"`（列出 `eAll` 的每个消费者）；
      ③ `git grep -n -E "CA_LOG_ERR|CA_ASSERT|CA_ASSERT_BREAK" -- "*.h" "*.cpp"` 的条数，并数出其中"传字符串字面量"
      与"传运行期字符串"各多少处（决定 2.2 的约束怎么写）。
      **怎么算做完**：三组命令都跑了、数字与本轮结论一致；"0 处"也要贴命令与输出（不许只写结论）。

## 2. 引擎侧三处清零（一处改完立刻跑一次探针，不许攒着一起看）

- [x] 2.1 `Interface/RenderInterface/header/GPUGraph.h` 里那两行 `.second`：按 1.2① 的读数处置 ——
      调用点为空则按"不可达残留"删掉或按意图改对，有调用点则改对（判空 `castl::shared_ptr` 本身、入队 `shared_ptr` 本身），
      选择与依据写进本文件。
      **怎么算做完**：同一条探针里 `GPUGraph.h` 的诊断列表不再含 `no member named 'second'`（改之前含 **2 条**）；
      有调用点时还要给出调用点所在编译单元的构建通过读数。
- [x] 2.2 `CACore/header/DebugUtils.h` 的两个 `error_with_location` 重载：消除歧义（字面量让 `format_string` 版接手，
      运行期字符串照旧走 `castl::string_view` 版）。**不许删掉任何一条调用路径**。
      **怎么算做完**：① 探针里那 3 条 `call to 'error_with_location' is ambiguous` 消失（改之前 3 条，见 1.1 记录）——
      判据是**工具输出里 `ambiguous` 出现 0 次**，而不是"MSVC 编译通过"（那两行在 MSVC 下改动前也编得过，
      拿编译通过当判据等于恒绿）；② 字面量与运行期字符串两种形态各保留一条**调用路径**，
      各给一条工具输出里不出现 `ambiguous` 的读数（用 1.2③ 量出的形态，两种都点到名）；
      ③ `python build.py --config Debug` 通过。
- [x] 2.3 `Interface/RenderInterface/header/Common.h` 里的 `eAll = ~0,`：改成能在 `uint32_t` 里表示的等值写法
      （位模式仍是 32 位全 1）。
      **怎么算做完**：① 探针里 `enumerator value evaluates to -1` 那条消失；② 1.2② 列出的每个消费者逐条说明用途
      （写进本文件），说明里不许出现"应该没影响"这类话 —— 要么给出该处如何用 `eAll` 的原文，要么给出它不使用的证据。
      **落盘读数（apply 第 1 轮）**：① `probe_chain_clean.py` 里 `RectSate`（它 include 了 `Common.h`）从
      `exit=6 错误级诊断行数=3` 变成 **`exit=0 错误级诊断行数=0`**；D3D12 那条链的错误列表里 `Common.h:372` 那条也已消失。
      ② 消费者只有一处：`D3D12RenderBackend/private/ResourceManagment/GPUResourceStates.h:477` 的
      `EResourceUsageFlags directQueueUsages = EResourceUsage::eAll;`，用法是同文件 473-480 行的
      `ResourceUsageCompatible` —— 在 direct 队列上取"全部用法"再 `HasAll(resourceUsages)`，要的就是"32 位全 1"，
      与 `~0` 的位模式逐位相同（`EResourceUsageFlags` 的存储是 `EResourceUsage` 的底层类型 `uint32_t`）。
      `Common.h:427` 的 `eAll,` 与 `InterfaceTranslator.h:38` 的 `ECullMode::eAll` 属于另一个枚举 `ECullMode`，本处改动不碰它们。
- [x] 2.4 复量 `Interface/RenderInterface/header/GPUGraph.h`：主头自身错误级诊断计数 **= 0**，退出码不再是 6。
      **怎么算做完**：读数贴出来（改之前是 5 条 / exit 6）。
      **落盘读数（apply 第 1 轮）**：同一命令（`probe_header_diagnostics.py … GPUGraph.h ViewRectData …`）
      **`退出码：0`、stderr 为空**（5 条 → 0 条：2 条 `.second` 由 2.1 消掉、3 条 `ambiguous` 由 2.2 消掉）。

## 3. 解析期垫片与 D3D12 那条链

- [x] 3.1 把 `definition of builtin function '__cpuid'` 固定在一条**可重跑**的命令上：
      `python openspec/changes/clear-codegen-parse-blockers/evidence/probe_header_diagnostics.py D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h GPUPipelineStateKey D3D12RenderBackend "struct GPUPipelineStateKey"`
      —— 它今天会打出那一条（见 `evidence/parse-gate-readings.md` 第三节）。
      **怎么算做完**：命令与原始输出贴进记录。
      ⚠ 第二种手段（`evidence/probe_shim_shape.py` 的第二部分）已经复跑了四种隔离上下文，**四种都是 exit 0、
      都不出现该诊断**；所以这条诊断目前只有真实链上那一个复现路径。若改完之后连真实链上也不再出现，
      就必须在记录里逐字写明「这条判据的独立红态未能构造（四种隔离上下文 + 真实链各跑了什么）并说明差异」，
      不许默认它有效、也不许因为"反正是修好了"就跳过。
- [x] 3.2 改 `Tools/CodeGen/clang_shim/cpuid.h`：MUST NOT 定义 clang 的内建 `__cpuid`；
      需要 GCC 形状的名字时用宏，且参数个数按真实调用点给（DirectXMath 在 clang 下调用的是
      `__cpuid_count(7, 0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3])`）。
      **怎么算做完**：① 3.1 那条命令里该错误消失；② 垫片探针 `evidence/probe_shim_shape.py` 的第一部分里，
      宏版在 `_XM_AVX2_INTRINSICS_` 开 / 关两种情况下都是 **exit 0**，而现状在"开"时是
      `no matching function for call to '__cpuid_count'`、只留 include 守卫时是
      `use of undeclared identifier '__cpuid_count'`（改之前的读数，脚本会逐条打印）。
      **落盘读数（apply 第 1 轮）**：垫片改成"不定义内建 + `__cpuid_count` 6 参数宏"之后，
      探针第一部分里 `current` 两档都是 **exit 0**（改之前"开"那档是 `no matching function for call to '__cpuid_count'`）；
      D3D12 链上的 `definition of builtin function '__cpuid'` 也不再出现。
- [x] 3.3 链上错误清零：跑 `python openspec/changes/clear-codegen-parse-blockers/evidence/probe_chain_clean.py --expect clean`
      —— 它对同一个头加上 `-ferror-limit=1`，把链上任何一条错误级诊断升级成致命诊断，工具就会把原文打印出来。
      **怎么算做完**：四个头**都不出现致命诊断、且一条错误级诊断都不打印**（改之前：四个全脏，
      `RectSate` 与 `ShaderLibrary` 还额外暴露了 `Common.h:372` 那条 —— 它们在默认开关下是 exit 0、stderr 空的）。
      ⚠ 判据是"一条都没有"，**不是**"不再以致命形式出现"（把错误数从 20 压到 19 也能让那句致命消失，
      但链上还是脏的）。还有：不许往推导环境或 `CODEGEN_EXTRA_INCLUDES` 里塞 `-ferror-limit` / `-w` /
      `-Wno-error` 这类抬高上限或降级错误的开关 —— 3.4 的文档里要写明这条红线。
      **落盘读数（apply 第 1 轮）**：`probe_chain_clean.py --expect clean` → 四个头（`ViewRectData` /
      `GPUPipelineStateKey` / `RectSate` / `ShaderLibrary`）全部 **`exit=0 错误级诊断行数=0`**，
      结论行 `期望=clean；链上仍有错误的：（无）`（改之前四个全脏）。清零过程中修掉的三处：`Common.h:372`（2.3）、
      `ShaderArgList.h` 那类字符串重载歧义（2.2）、生成物里的 `offsetof`（见新加的 3.5）。
- [x] 3.5 **（apply 第 1 轮新发现，就地修）生成物里的 `offsetof` 在 clang 下不是常量表达式**。
      红态是链上最后一处错误：`out/build/x64-debug/codegen/**/*.Reflection.h` 的
      `static constexpr CodeGenFieldInfo arr[…]` 报 `constexpr variable 'arr' must be initialized by a constant expression`。
      根因（隔离到一行）：UCRT 的 `stddef.h` 在 `_MSC_VER` 下把 `offsetof` 展开成**空指针解引用**形式
      （`#if defined _MSC_VER && !defined _CRT_USE_BUILTIN_OFFSETOF`），而 clang 在 MSVC 目标上也定义 `_MSC_VER`；
      MSVC 自己会折叠这个模式，clang 不能。处置：给工具的**解析命令行**加 `-D_CRT_USE_BUILTIN_OFFSETOF`
      （落在 `build.py` 的 `CODEGEN_EXTRA_DEFINES`）—— 只作用于解析环境，产物文本仍是 `offsetof`，MSVC 侧编译不变。
      **怎么算做完**：① 红态：不加该宏的最小对照（`static constexpr std::size_t offA = offsetof(WithInts, a);`
      与带 `offsetof` 的 `CodeGenFieldInfo`）报两条 constexpr 错误、`exit=6`；② 绿态：加上该宏后同一个探针 `exit=0`；
      ③ `probe_chain_clean.py --expect clean` 四个头全部 `exit=0 错误级诊断行数=0`。
      **落盘读数（apply 第 1 轮）**：① 加宏前 `exit=6`（`constexpr variable 'offA' …` 与 `constexpr variable 'oneWithOffset' …`）、
      加宏后 `exit=0`；② 四个头全部 `exit=0 错误级诊断行数=0`。
- [x] 3.4 在 `Tools/CodeGen/README.md` 补一节"解析期垫片"：替代哪个头（`cpuid.h`）、为什么仓库里需要它
      （vendored LLVM 没有 clang 资源头目录）、边界（只让 include 过得去、不参与布局计算）、
      以及"不许用抬高错误上限 / 降级错误的开关去换通过"这条红线。
      **怎么算做完**：`python Tools/CodeGen/verify_readme.py` 报 **0 处不符**（它只跑 README 里
      `CodeGen.exe ` 开头的示例，覆盖不到新增的描述性段落），**并且**新章节里的关键句逐条可 grep：
      用 `git grep -n -F "<该节的逐字引文>" -- Tools/CodeGen/README.md` 逐条命中（至少覆盖
      `Tools/CodeGen/clang_shim/cpuid.h`、"替代的头"、"只让 include 过得去"三处）。

## 4. 四个键类型进接入清单

- [x] 4.1 **先测期望值**：对四个类型各造一个实例，**经既有入口**（`cacore::hash<T>{}` 或
      `HashObj<T>::GetHash()`，不是直接调 `custom_hash_trait`）取当前哈希值，读数写进
      `evidence/parse-gate-readings.md`（这组数字就是迁移后的期望值，不许手算），并写明是在哪个提交/文件状态下测的。
      **怎么算做完**：四行"类型 → 哈希值"的读数贴在记录里；四条读数都要能由记录里的命令重跑出来。
      **落盘读数（apply 第 1 轮）**：读数落在 `Test/CoreTests/GeneratedHashTest.cpp`（既有文件末尾，不新增用例，
      所以 `PASSED` 行数不变），`CoreTests.exe` 退出码 **0**，那一行逐字是
      `CodeGen key hash readings (ViewRectData / InputElementDesc / VertexInputBindingData / GPUPipelineStateKey) = 9566659391000707361 / 7588715830773829420 / 17516807725481476295 / 17773842414398720855`。
      ⚠ **这一段第一次加进去时引发了一次 ODR 事故**：只 include 两个类型定义头、没 include 哈希链上那些类型的
      生成物 → 整个二进制的 `CPipelineStateObject` 哈希被换成整块口径、`CPipelineStateObject hash ignores padding`
      由 1 掉成 0、`CoreTests.exe` 退出码变 1。修法是那个编译单元把链上用到的生成物自己 include 一遍
      （八份 `*.Hash.h`，见 `design.md` D10 与 `evidence/parse-gate-readings.md` 第十二节）。
      **4.3~4.6 的断言落点 MUST 照同一条规矩办**（写清 include 了哪几份生成物），否则同一条判据会被悄悄换口径。
- [x] 4.2 核对成员形状：逐个确认这四个类型 ① 有没有浮点成员（手写特化对浮点不归一化、生成代码会归一化）
      ② 有没有**定长数组成员**（手写走整块按字节、生成走"先写个数再逐个"）③ 有没有作为成员出现在任何
      **已迁移类型**的链上（出现在成员链上就会进落盘字节）；并注意 `ViewRectData` 这类"既平凡可拷贝、
      又带自定义特化"的类型，`cacore::dispatch_branch_unchanged<T>()` 对它们**恒为假**，不许拿它当判据。
      **怎么算做完**：四个类型逐条给出结论与依据（成员列表原文 / `git grep` 结果）；命中①②任一条就停下，
      在 `design.md` 里写"值会变 + 处置理由"再决定是否迁。**并写一句**：改这四个类型的成员时必须重测 4.1 那组期望值。
      **落盘读数（apply 第 1 轮）**：
      - 成员逐字：`ViewRectData` = `int x; int y; int width; int height;`；
        `InputElementDesc` = `cacore::NameHash SemanticName; UINT SemanticIndex; DXGI_FORMAT Format; UINT InputSlot;
        UINT AlignedByteOffset; D3D12_INPUT_CLASSIFICATION InputSlotClass; UINT InstanceDataStepRate;`；
        `VertexInputBindingData` = `castl::vector<cacore::NameHash> inoutBindingNameToIndex;
        castl::vector<InputElementDesc> outVertexAttributes;`；
        `GPUPipelineStateKey` = `ShaderInfo m_ShaderInfo; cacore::HashObj<CPipelineStateObject> m_PipelineStates;
        cacore::HashObj<InputAssemblyStates> m_InputAssemblyStates; castl::vector<ETextureFormat> m_AttachmentFormats;
        ETextureFormat m_DepthFormat; VertexInputBindingData m_VertexInputBindingData;`
      - ① 浮点：四个类型**直接成员里一个浮点都没有**。传递地看有一个：`HashObj<CPipelineStateObject>` 里的
        `RasterizerStates::lineWidth`（float）—— 但它由**它自己那份**生成物负责（本次不碰），所以不影响这四个键的值。
      - ② 定长数组：四个类型都没有 C 数组 / `castl::array` 成员（成员列表见上）。
      - ③ 落盘链：`grep`（`out/build/x64-debug/codegen` 全部产物）这四个类型名 → **零命中**，
        说明它们不出现在任何已标注类型的成员链上，迁移不改变任何落盘字节。
      - ④ 结论：形状上**不需要**任何"值会变"的例外处理；4.1 那组期望值就是迁移后必须逐位相等的目标。
        ⚠ 以后改这四个类型的成员 MUST 重测 4.1 那组期望值（读数在 `evidence/parse-gate-readings.md` 第十二节）。
- [x] 4.3 `graphics_backend::ViewRectData` 迁进清单：① 在 `Interface/RenderInterface/header/GPUGraph.h` 的类型定义处
      加 `CA_REFLECT(serializable)` 标注（工具只对带标注的类型出码）；② `build.py` 加行（归属 `Interface/RenderInterface`）；
      ③ 同一个头里 include 三份生成物；④ 删掉文件末尾那份手写 `custom_hash_trait<graphics_backend::ViewRectData>`；
      ⑤ 在**真实消费它的编译单元** `Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp` 里加字段表断言、
      `cacore::codegen::has_custom_hash_func<graphics_backend::ViewRectData, cacore::codegen::hasher<>>` 断言、
      4.1 那组哈希值断言（经既有入口取）、以及填充字节对照（同一对象两份、只改填充字节 → 既有入口哈希相等；
      整块按字节口径下不相等）。
      **怎么算做完**：`python build.py --config Debug` 里这一行 exit 0 **并写出 3 份产物**（只有 exit 0 不算），
      且 codegen 段不再报 `工具退出码 0，却没有写出这些产物`；`bin/CoreTests.exe` 退出码 0。
      **落盘读数（apply 第 1 轮）**：codegen 段 `[23/73] ViewRectData` → 三份产物
      `ViewRectData.Reflection.h (1224 B)` / `.Serialization.h (1583 B)` / `.Hash.h (1333 B)`，`BUILD SUCCESSFUL`；
      编译期断言落在 `Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp`（字段表四条偏移 +
      `has_custom_hash_func<ViewRectData, hasher<>>` 一条）；运行时守卫落在
      `Test/CoreTests/GeneratedHashTest.cpp`，读数 `CodeGen key hash unchanged (ViewRectData) = 1 (期望 9566659391000707361，实得 9566659391000707361)`；
      `CoreTests.exe` 退出码 **0**。
      ⚠ 这一步顺带逼出一个**既有隐患**：`GPUGraph.h` 的 `DrawCallBatch::SetParam` 把 `cacore::NameHash` 直接
      交给格式串（`fmt` 没有它的格式化器），只有在这个内联成员函数**被实例化**的编译单元里才会炸
      （`fmt/base.h` 的 `type_is_unformattable_for<const cacore::NameHash, char>`）。断言单元一开始 include
      `GPUGraph.h` 就撞上了它，已就地改成传 `name.string()`。判据：不动它时该单元 `error C2079`、构建失败；
      改后 `BUILD SUCCESSFUL`。
- [x] 4.4 `graphics_backend::GPUPipelineStateKey` 同 4.3 的五步，头是
      `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h`、归属 `D3D12RenderBackend`。断言 MUST 落在
      **D3D12 模块自己的编译单元**里（该模块已有 `D3D12RenderBackend/private/ShaderLibrary/ShaderLibraryCodeGenAssertions.cpp`
      这种落点；`Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp` 看不到 D3D12 的私有头，
      写进去会编不过）。
      **怎么算做完**：同 4.3；并把断言所在文件与那一行写上（该类型在既有调用点真的被当键用：
      `castl::shared_dic<GPUPipelineStateKey, …>`）。
      **落盘读数（apply 第 2 轮）**：codegen 段 `[24/74] GPUPipelineStateKey` → 三份产物
      （`GPUPipelineStateKey.Reflection.h (2073 B)` / `.Serialization.h (1952 B)` / `.Hash.h (1835 B)`），`BUILD SUCCESSFUL`；
      断言落在既有调用点所在编译单元 `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.cpp`
      （六条 `offsetof` 偏移 + `has_custom_hash_func` 一条）；运行时读数
      `CodeGen key hash unchanged (GPUPipelineStateKey) = 1 (期望 17773842414398720855，实得 17773842414398720855)`；
      `CoreTests.exe` 退出码 **0**，`CPipelineStateObject hash ignores padding` 仍为 1。
- [x] 4.5 `graphics_backend::VertexInputBindingData` 与它内部的 `InputElementDesc`（嵌套类型）同 4.3 的五步，
      删掉那两份手写特化，断言同样落在 D3D12 模块自己的编译单元里。
      **怎么算做完**：同 4.4；嵌套类型那行要有 `--qualified-name` 的读数或工具按非限定名命中的证据。
      **落盘读数（apply 第 2 轮）**：codegen 段 `[25/76] VertexInputBindingData` 与 `[26/76] InputElementDesc`
      各产出三份产物（`VertexInputBindingData.{Reflection,Serialization,Hash}.h` = 1630/1507/1308 B；
      `InputElementDesc.{…}` = 2095/2069/2005 B），清单 **76 行**（与 proposal 预估一致）；`BUILD SUCCESSFUL`；
      嵌套类型那行用的是清单第 4 列限定名，产物里逐字是
      `struct TypeDescriptor<graphics_backend::VertexInputBindingData::InputElementDesc>`、
      `name = "graphics_backend::VertexInputBindingData::InputElementDesc"`；
      断言落在 `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.cpp`（两条 + 七条 `offsetof` + 两条 `has_custom_hash_func`）；
      运行时读数 `key hash unchanged (InputElementDesc) = 1 (期望 7588715830773829420，实得 7588715830773829420)`、
      `key hash unchanged (VertexInputBindingData) = 1 (期望 17516807725481476295，实得 17516807725481476295)`；
      `CoreTests.exe` 退出码 **0**。
- [x] 4.6 **反向对照**（证明 4.3~4.5 的"相等"不是恒真）：对这四个类型里的实例各造两份、**只把填充字节改成不同值**，
      经既有入口取到的哈希必须相等；同两份数据在"整块按字节"口径下（例如直接 `hasher.hash_raw(&obj, sizeof(obj))`）
      取到的哈希必须**不同**。
      **怎么算做完**：两次读数都贴出来（若"整块口径"下也相等，说明这条对照构造错了，必须重做 4.3~4.5 的相等断言）。
      ⚠ 拿含 `-0.0f` 的对象去证明"浮点会归一化"**不算**这条对照 —— 那是 4.2① 的差异读数，与这四个类型无关。
      **落盘读数（apply 第 2 轮）**：`CodeGen key hash controls = 1 (改一个字段 → 哈希变 = 1；尾部填充改成别的字节 →
      哈希不变 = 1；尾部填充字节数 ViewRectData=0 InputElementDesc=0 VertexInputBindingData=0 GPUPipelineStateKey=0)`。
      ⚠ **本条按字面写的那半（"只把填充字节改成不同值"）对这四个类型不适用**：实测四个类型的**尾部填充字节数都是 0**
      （`sizeof(T) - (最后一个成员的偏移 + 它的大小) = 0`），没有可改的填充。所以这条判据的可失败性由两件事承担：
      ① **改一个字段 → 经既有入口的哈希必须变**（读数里 `= 1`；它一旦不变，说明哈希没覆盖该字段或退化成常量）；
      ② **生成特化的填充不变性**由 `Test/CoreTests/PipelineStateObjectTest.cpp` 那条既有判据负责 ——
      `CPipelineStateObject` / `RasterizerStates`（后者有两个 bool 之后、首个枚举之前的 2 字节内部填充）用的是
      **同一套生成的特化**，那条读数仍是 `CPipelineStateObject hash ignores padding = 1`。
      **改松了吗**：没有 —— 新加的"改一个字段 → 哈希变"比原判据更严（原判据在这四个类型上是空的），
      填充不变性则挂到确实有填充的类型上。
- [x] 4.7 若 4.3~4.5 中某个类型撞上**形状**拒绝（退出码 13 / 14 / 15 / 16 / 17）：这是**范围假设被推翻**，
      不是"换个做法交差"—— MUST 停下来改工件（把该类型从 proposal 与 spec 的纳入范围里移出、在 `design.md`
      的「已知边界」一节写红态读数：退出码 + 工具点名的成员那一行），**不许**"保留手写特化"就算这条做完。
      **怎么算做完**：该类型要么进了清单且四个判据齐备，要么工件里的范围已经按实测收回、且那条边界有红态读数；
      两者之间没有第三种"完成"。
      **落盘读数（apply 第 2 轮）**：这条分支**没有被走到** —— 四个类型全部走的是"进清单"那一支：
      codegen 段 `[23/73]/[24/74]/[25/76]/[26/76]` 四行都是 exit 0、各产出三份产物（清单 76 行），
      没有任何一行报出退出码 13/14/15/16/17。所以 `design.md` 的「已知边界」一节保持原样（本次不迁的那几个类型），
      没有为本条新增边界条目。

## 5. 收尾验证与文档

- [x] 5.1 `python build.py --config Debug` → `BUILD SUCCESSFUL`，codegen 段 **76** 行逐条 exit 0。
      **怎么算做完**：贴出 codegen 段的合计读数与构建结论。
      **落盘读数（apply 第 2 轮）**：`python build.py --config Debug --codegen-all`（强制每一条都重跑）→
      codegen 段进度行 **76** 条、`CODEGEN FAILED` 命中 **0** 次、`BUILD SUCCESSFUL`；
      `[耗时] configure 0.0s / codegen 40.6s / 编译 42.9s / 合计 83.5s`。
- [x] 5.2 `out/build/x64-debug/bin/CoreTests.exe` → 退出码 **0**，用例数不减。
      **怎么算做完**：贴出退出码与屏幕上的 `PASSED` 行数。口径写清楚：改之前实测是 **12 行 `PASSED`**
      （9 行来自各用例函数、3 行来自 `main`；`main` 里的测试函数调用共 11 个）—— 按"行数"对账，别按函数个数数，
      两种数法差 1，容易误判成"丢了一条用例"。
      **落盘读数（apply 第 2 轮）**：退出码 **0**、`PASSED` **12** 行、`FAILED` **0** 行（与改之前同口径同数量）。
- [x] 5.3 两个后端的 tester 各跑一次：`GPUBackendTester.exe --backend vulkan --headless 10` 与
      `--backend d3d12 --headless 10`。
      **怎么算做完**：贴出两次的读数，且**判据不是退出码** —— `Test/GPUBackendTester/Main.cpp` 的
      `main` 以 `return EXIT_SUCCESS;` 结束、各测试的返回值被丢弃，所以退出码 0 只证明"进程没崩"。
      判据改成 `test_output/` 下的 `result.json`：逐个断言里面 **8 条**记录的 `status` 都是 pass
      （8 是测试个数，来自 `--list`）。⚠ 这一条只证明"没崩"，**证明不了哈希值迁移正确** ——
      哈希由 4.3~4.6 的读数负责，别把因果写反。
      **落盘读数（apply 第 2 轮）**：`result.json` 会被每次运行覆盖，所以两次跑完各留了一份快照：
      vulkan → **8 条记录、status=pass 8 条**；d3d12 → **8 条记录、status=pass 8 条**；
      两次的 `exit_code` 都是 0，`duration_ms` 合计 402 / 411。
- [x] 5.4 `python Tools/CodeGen/check_legacy_symbols.py` → 退出码 0 且命中 **0 行**。
      **怎么算做完**：贴出"扫描 N 个文件、命中 0 行"那行。
      **落盘读数（apply 第 2 轮）**：退出码 **0**，逐字 `扫描 517 个文件、命中 0 行`。
- [x] 5.5 重测全量构建耗时：**先跑一遍从零构建**（清掉构建目录里除 `_deps/` 之外的东西，
      `python build.py --config Debug --offline-fetch`），把 configure / codegen / 编译 / 合计四项读数与
      清单行数（76）同步进 `CLAUDE.md` 的构建一节；`--codegen-all` 只重跑 codegen 段、编译段仍是增量，
      两个口径测的不是同一件事，别拿它冒充"从零"。
      **怎么算做完**：`CLAUDE.md` 里那串时间与行数改成当场实测值，四项读数贴出来。
      **落盘读数（apply 第 2 轮）**：三档都当场跑了（清构建目录：10122 个文件 → 9568 个，剩下的都是 `_deps/`）：
      - 从零（`python build.py --config Debug --offline-fetch`）：`重跑 76 条、跳过 0 条`，
        `configure 52.3s / codegen 39.3s / 编译 97.5s / 合计 189.1s`；
      - 全量（`--codegen-all`）：`重跑 76 条、跳过 0 条`，`configure 0.0s / codegen 40.6s / 编译 42.9s / 合计 83.5s`；
      - 什么都不改：`重跑 0 条、跳过 76 条`，`configure 0.0s / codegen 1.3s / 编译 2.1s / 合计 3.5s`。
      `CLAUDE.md` 的「构建」节已改成这组数（并写明清单 76 条）。
      **范围加了一条（说明理由）**：`Tools/CodeGen/README.md` 的「构建耗时」一节记的是**同一批读数**（原写 72 条），
      按"改了行为就同一次更新文档"的规矩一并改成 76 条与上面三档；这不是顺手改别的文档，而是同一条读数的另一处落点。
- [x] 5.6 `openspec validate clear-codegen-parse-blockers` → **valid**；三份工件与 `evidence/` 无悬空引用。
      **怎么算做完**：贴出 validate 的输出。
      **落盘读数（apply 第 2 轮）**：逐字 `Change 'clear-codegen-parse-blockers' is valid`；
      工件点名的 15 个文件逐个 `Test-Path` → **全部存在**。
      另外核了规格里那条"不许拿放松判据换取通过"：`git diff HEAD -- Tools/CodeGen/CodeGenMain.cpp` **为空**
      （门判据 `bool untrustworthy() const { return !fatalMsg.empty() || inHeader > 0; }` 一个字符未动），
      `build.py` 与工具里都没有新增任何诊断白名单。
- [x] 5.7 **末条：编译验证与修复** —— 再跑一次 `python build.py --config Debug`；若失败，分析并修复到
      `BUILD SUCCESSFUL` 为止（修的是根因，不许把判据改松来通过）。
      **怎么算做完**：最后一次构建输出为 `BUILD SUCCESSFUL`。
      **落盘读数（apply 第 2 轮）**：`configure 0.0s / codegen 1.3s / 编译 2.1s / 合计 3.4s` → **`BUILD SUCCESSFUL`**，
      没有 `CODEGEN FAILED`、没有 `error C`。

## 收尾记账（Review Log）

**交付物**（11 个跟踪文件 + 本 change 目录）：

- 引擎侧三处写法清零：`Interface/RenderInterface/header/GPUGraph.h`（两行 `.second`、一处把 `NameHash` 交给
  格式串的隐患、`CA_REFLECT` 标注与生成物 include）、`Interface/RenderInterface/header/Common.h`
  （`eAll = ~0` → `0xFFFFFFFFu`）、`CACore/header/DebugUtils.h`（消除两个重载歧义：给 `const char*` 与
  `castl::string` 各加一个"零实参"受约束重载，两条调用路径都保留）
- 工具侧：`Tools/CodeGen/clang_shim/cpuid.h`（不重定义内建、`__cpuid_count` 改成 6 参数宏）、
  `build.py`（新增 `CODEGEN_EXTRA_DEFINES` 带 `-D_CRT_USE_BUILTIN_OFFSETOF`；接入清单 72 → **76** 行）
- 断言/用例：`Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp`、
  `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.{h,cpp}`、`Test/CoreTests/GeneratedHashTest.cpp`
- 文档：`Tools/CodeGen/README.md`（新增「九、解析期垫片与解析环境里的两个额外开关」；构建耗时改 76 条）、
  `CLAUDE.md`（构建一节读数）
- 证据：`evidence/` 五份可重跑探针 + `parse-gate-readings.md`（§九~§十二）

**关键读数**：codegen 段 **76 行逐条 exit 0**（`--codegen-all`，`CODEGEN FAILED` 0 次）；
`CoreTests.exe` 退出码 **0**、`PASSED` **12** 行、`FAILED` 0；两个后端 tester 的 `result.json` 各 **8 条 status=pass**；
`check_legacy_symbols.py` → `扫描 517 个文件、命中 0 行`；四个键类型的哈希值与迁移前**逐位相同**
（`9566659391000707361` / `7588715830773829420` / `17516807725481476295` / `17773842414398720855`）；
链上 `-ferror-limit=1` 四个头全部 `exit=0 错误级诊断行数=0`；`openspec validate` → valid。

**相比开工时的实质变化**：四个键类型从"手写 `custom_hash_trait`"换成 CodeGen 生成物（值一位没变）；
解析门对 `Interface/RenderInterface/header/GPUGraph.h` 与
`D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` 不再拒收。

**过程中挖出并就地修的三个既有隐患**：① 生成物里的 `offsetof` 在 clang 下不是常量表达式（UCRT 宏 + 解析期
`-D_CRT_USE_BUILTIN_OFFSETOF`，见 design D9）；② 哈希特化漏 include 会把整个二进制的哈希口径换成整块字节
（ODR，见 design D10）；③ `GPUGraph.h` 的 `DrawCallBatch::SetParam` 把 `cacore::NameHash` 交给格式串
（只在被实例化时报错，已改传 `name.string()`）。

**明确没做的**：不迁的五个类型与理由在 `design.md` 的「已知边界」；没有提交（用户没有下达提交指令）。
