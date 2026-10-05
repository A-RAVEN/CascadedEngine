## Why

新 CodeGen 系统现在只覆盖了"容器"那一层，其余全部委托给旧的运行时（就是 `cacore::serializer` 那一套）：生成的代码里每个成员都是 `s.serialize(obj.x)` / `d.deserialize(obj.x)`，`s` 就是旧运行时里的 `cacore::serializer`。实测生成物引用的旧符号只有五个 —— `cacore::serializer`、`cacore::deserializer`（`Serialization.h`）、`careflection::containerStates`（`Reflection.h`）、`cacore::custom_hash_trait`、`cacore::has_custom_hash_func`（`Hasher.h`）—— 但只要这五个还在被引用，那三份旧头就删不掉，"用 CodeGen 替代旧方案"就没有完成。

卡住的地方不止"还没搬完"，还有三处结构性缺陷，它们决定了**有些类型（例如容器里装结构体）永远只有旧方案能用**：

1. **读位置不公开**：`cacore::serializer` 的缓冲区、`cacore::deserializer` 的偏移都是私有成员且没有取值方法。生成代码既不能把自己的缓冲区交给另一个类型的生成序列化器，也不能在正确偏移上新建反序列化器 —— 于是"容器里装结构体（该结构体内部又有无序容器）"这类类型只能拒绝（退出码 12），而旧方案在同样的类型上不会报错，直接按遍历顺序写，写出的字节不稳定。保留这条拒绝分支，就等于承认做不到完全替代。
2. **哈希接不上**：`Hasher.h` 的分派顺序是"平凡可拷贝优先"（第 86 行），而生成物提供的钩子是 `custom_hash_trait<T>` 特化。纯 POD 的类型（例如 D3D12 里正被 `HashObj<CPipelineStateObject>` 使用的 PSO 状态结构）走整块字节那条分支，**生成的特化永远不会被调用** —— 不报错、不崩溃，只是新哈希静默不生效。
3. **工具侧的空白**：标记通道只服务命名空间作用域的类型，而引擎里真实存在被老宏标注的嵌套类型（`DepthStencilStates::StencilStates`、`cahash::sha256_hash::result_type`），现在标不上（退出码 4）；D3D12 模块由 build.py 推导出的编译环境缺 `directx/d3dx12.h`，要解析那边的头就会以退出码 6 退出。

已经核过、可以放心动的一点：构建内共**五个** `custom_hash_trait` 特化 —— `HashObj` / `NameHash` / `PathHash`（都在 `Hasher.h`）、`ComPtr<ID3DBlob>`（`D3D12RenderBackend/private/D3D12Includes.h`）、`graphics_backend::VulkanShaderResourceSet`（`VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h`）—— 对应的类型**没有一个是平凡可拷贝**（`HashObj` 第 199 行、`NameHash` 第 523 行、`PathHash` 第 364 行都自己写了拷贝构造，`ComPtr` 有析构，`VulkanShaderResourceSet` 内含无序映射）。所以把分派顺序调换不会改动任何既有哈希值；design 的核对表对每个宿主逐条给出 `static_assert(!std::is_trivially_copyable_v<宿主>)`。

## What Changes

分五步，每步产出的都是可核的东西：

1. **新运行时自带读写位置**。新增一套由 CodeGen 自己拥有的读写对象（写：只追加；读：字节起点 + 长度 + 已读位置，位置是显式参数）。生成物不再构造 `cacore::serializer` / `deserializer`。完成后，"容器里装结构体"不再需要拒绝：生成代码可以把当前读到的位置交给另一个类型的生成代码，由它接着往下读。写出的字节与现在保持逐字节一致，以便迁移期用逐字节比对验收。
2. **生成物脱离旧头**。生成物引用的五个旧符号全部换成新运行时的等价物（容器判据、哈希钩子、特化检测）。这一步做完，生成物与三份旧头解耦，旧头仍可为旧调用点服务。
3. **新哈希在旧调用点生效**。把 `Hasher.h` 第 86–96 行的分支顺序调换（"有自定义特化"优先于"平凡可拷贝"），使生成的特化能在 `HashObj<...>` / `aggregateHasher` 的既有调用点生效，**不改 `HashObj<...>` 的那 70 处调用点**（实测计数，不是估的）。
4. **工具补齐**：支持命名空间作用域之外的嵌套类型标注；命令行类型名统一按最后一个非限定名解析（并在文档里写明）；给 D3D12 模块补上缺失的 include 目录，使其头文件可被解析。
5. **按模块迁移并删除旧头**。照 `GPUBufferDescriptor` 已跑通的套路逐类型搬（头里只留新标记，老宏挪到消费方 .cpp 让 ABI 守卫继续生效），最后把 `NameHash` / `PathHash` / `HashObj` 等宿主类型搬进新头，删除 `Reflection.h` / `Serialization.h` / `Hasher.h`。
6. **两类形状的改动要单列**（对应 design 的 D8）：① 私有成员的类型（`ImageHandle` / `BufferHandle`、`ShaderBindingBuilder` 一族，现状用 `CA_PRIVATE_REFLECTION`）要加一个与新系统对应的 friend 入口，这会改这些引擎头的源码；② 含裸指针 / 托管指针成员的类型（老后端 `VulkanPipelineObject` 一类）要么按值语义改写结构，要么在本 change 里显式排除并附实测读数。这两类的工作量算在本 change 内，不许当成"迁移时顺手做"。

**完成条件（可核、能失败）**：下面三条**同时**成立才算完成 ——

1. **生成物与生产代码零引用**：`Tools/CodeGen/check_legacy_symbols.py` 对生成物（**构建目录** `out/build/<预设>/codegen/<模块>/`；源码树里的 `generated/` 目录已由 `improve-build-time` 删除）与引擎源码（后缀 `.h/.hpp/.cpp/.cxx`，排除 `openspec/`、`out/`、脚本自身、以及下面第 3 条的白名单文件）逐符号扫描，命中 0 行；符号表固定为：`cacore::serializer`、`cacore::deserializer`、`cacore::batch_deserializer`、`cacore::aggregateHasher`、`cacore::custom_hash_trait`、`cacore::has_custom_hash_func`、`careflection::containerStates`、`careflection::managed_wrapper_traits`、`CATypeDescriptor`、`CA_REFLECTION`、`CA_REFLECTION_TEMPLATE`。脚本必须打印"扫描 N 个文件、命中 0 行"，且 **N 不为 0**（N=0 就是扫到了空集；脚本在找不到任何生成物目录时会直接以退出码 2 报错）。
2. **三份旧头在文件系统上不存在**：用文件系统枚举（不是 `git ls-files`）确认 `CACore/header/Reflection.h`、`Serialization.h`、`Hasher.h` 三个路径都不存在（`git rm --cached`、改名、搬进新头都不算），且 `python build.py --config Debug` → `BUILD SUCCESSFUL`、`CoreTests.exe` 退出码 0 且用例数不少于开工前（逐条列出用例名）。
3. **旧实现不许换个名字留下**：扫描旧实现的指纹（`visit_members`、`CATypeMemberDesc`、`containerInfo`、`SubmitAndWait`、`Object must be trivially copyable`、`hash_one`、`managed_pointer_traits`），命中 0 行；并用 `git log --diff-filter=R --name-status` 断言三份旧头是**删除**而不是改名 / 搬迁。

**唯一的豁免（2026-10-04 修正：收尾时必须为 0 行）**：原计划给"消费方 `.cpp` 里为 ABI 守卫保留的老宏"留一份逐行白名单。实测这条留不住 —— 那个宏（`CA_REFLECTION`）和它建出来的 `CATypeDescriptor` 都定义在 `Reflection.h` 里，而 `Reflection.h` 正是第 2 条要求从文件系统上消失的头；头一删，消费方那几行就编译不过。所以收尾口径改成：**白名单必须是 0 行**，消费单元的 ABI 断言改成"与写死的期望值比"（字段数、字段名、`offsetof` 逐个断言 —— `CPipelineStateObjectAssertions.cpp` 与两个 `ShaderLibraryCodeGenAssertions.cpp` 已经是这个写法），`CodeGenMeta.h` 里 `static_assert_abi_consistency<T>()` 中"与老描述符比字段数"的那一支随之删除。这批工作量记在 tasks 的 9.6。

## Capabilities

### New Capabilities

- `codegen-runtime`: CodeGen 自有的读写与哈希设施 —— 自带读写位置的对象、标量与字符串的字节契约、容器与定长数组的处理、哈希对象与"元素用自己的特化"规则、容器判据。它取代旧运行时在生成物里的角色，并为"类型套类型"提供递归调用能力。
- `codegen-type-migration`: 引擎已有类型从老宏与旧运行时迁到 CodeGen 的过程规范 —— 双份描述（头里新标记 + 消费方 .cpp 里老宏）的过渡形态、逐类型验收方式、嵌套类型与私有成员类型的处理规则、以及旧头删除的判据。

### Modified Capabilities

- `codegen-framework`: 生成物不再调用 `cacore::serializer` / `deserializer`，也不再引用 `careflection::containerStates`；序列化入口改为接收缓冲区与游标；容器里装结构体不再拒绝（退出码 12 撤除）；定长数组（无 `clear()`）由读取路径按位置覆写而不是先清空；标记通道支持嵌套类型；命令行类型名解析口径写进文档。

## Impact

- **新增**：CodeGen 自有的运行时头（读写对象、哈希对象、容器判据），以及它们的对外文档与示例。
- **改动**：`Tools/CodeGen/CodeGenMain.cpp`（生成代码的那段工具改为调用新运行时、支持嵌套类型的标注、类型名解析规则、几条以错误码结束的分支调整）、`build.py`（D3D12 等模块的编译环境补齐）、`CACore/header/Hasher.h` 的分派顺序（两行）。
- **迁移**：`Interface/RenderInterface/header/CPipelineStateObject.h`、`ShaderBindingBuilder.h`、`ShaderResourceHandle.h`、`VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h`、`D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h` 等已标注类型，以及它们的消费方 .cpp。
- **改引擎自己的类型源码**（design 的 D8，工作量算在本 change 内）：私有成员的类型加 friend 入口（`ShaderResourceHandle.h`、`ShaderBindingBuilder.h` 里现有的 `CA_PRIVATE_REFLECTION` 位置），含指针 / 托管指针成员的类型按值语义改写结构或显式排除并附读数。
- **还要处置四类旧用**（不处置完成条件就不可能成立）：工具自身的 `CA_REFLECTION` 识别与只支持反射的文本通道（`Tools/CodeGen/CodeGenMain.cpp`）、`Tools/CodeGen/README.md` 与示例里的旧通道说明、`Test/CoreTests` 里 6 个依赖旧宏的用例、以及 `CACore/header/uenum.h` 的类模板描述符 `CA_REFLECTION_TEMPLATE`（工具现在明确不支持类模板，必须单独给出迁移办法或把它列成保留项并附实测不可行的证据）。
- **删除**：`CACore/header/Reflection.h`、`Serialization.h`、`Hasher.h`（完成条件达成后），`NameHash` / `PathHash` / `HashObj` 等宿主类型迁到新头。
- **风险**：哈希值语义变化（整块字节 → 按字段）会影响所有用旧哈希值当键的内存字典 —— 今天没有任何哈希值写到磁盘，进程内会按新规则自洽重算；`HashObj<...>` 的相等比较按 design 的 D4 走生成特化的 `equal`（已定，不是待定），否则会出现"哈希按字段、比较按字节"的错配。
