## Why

新 CodeGen 系统现在只覆盖了"容器"那一层，其余全部委托给旧的运行时（就是 `cacore::serializer` 那一套）：生成的代码里每个成员都是 `s.serialize(obj.x)` / `d.deserialize(obj.x)`，`s` 就是旧运行时里的 `cacore::serializer`。实测生成物引用的旧符号只有五个 —— `cacore::serializer`、`cacore::deserializer`（`Serialization.h`）、`careflection::containerStates`（`Reflection.h`）、`cacore::custom_hash_trait`、`cacore::has_custom_hash_func`（`Hasher.h`）—— 但只要这五个还在被引用，那三份旧头就删不掉，"用 CodeGen 替代旧方案"就没有完成。

卡住的地方不止"还没搬完"，还有三处结构性缺陷，它们决定了**有些类型（例如容器里装结构体）永远只有旧方案能用**：

1. **读位置不公开**：`cacore::serializer` 的缓冲区、`cacore::deserializer` 的偏移都是私有成员且没有取值方法。生成代码既不能把自己的缓冲区交给另一个类型的生成序列化器，也不能在正确偏移上新建反序列化器 —— 于是"容器里装结构体（该结构体内部又有无序容器）"这类类型只能拒绝（退出码 12），而旧方案在同样的类型上不会报错，直接按遍历顺序写，写出的字节不稳定。保留这条拒绝分支，就等于承认做不到完全替代。
2. **哈希接不上**：`Hasher.h` 的分派顺序是"平凡可拷贝优先"（第 86 行），而生成物提供的钩子是 `custom_hash_trait<T>` 特化。纯 POD 的类型（例如 D3D12 里正被 `HashObj<CPipelineStateObject>` 使用的 PSO 状态结构）走整块字节那条分支，**生成的特化永远不会被调用** —— 不报错、不崩溃，只是新哈希静默不生效。
3. **工具侧的空白**：标记通道只服务命名空间作用域的类型，而引擎里真实存在被老宏标注的嵌套类型（`DepthStencilStates::StencilStates`、`cahash::sha256_hash::result_type`），现在标不上（退出码 4）；D3D12 模块由 build.py 推导出的编译环境缺 `directx/d3dx12.h`，要解析那边的头就会以退出码 6 退出。

已经核过、可以放心动的一点：现有四个 `custom_hash_trait` 特化（`HashObj`、`NameHash`、`PathHash`、`ComPtr<ID3DBlob>`）对应的类型**没有一个是平凡可拷贝** —— `HashObj` 第 199 行、`NameHash` 第 523 行、`PathHash` 第 364 行都自己写了拷贝构造，`ComPtr` 有析构。所以把分派顺序调换不会改动任何既有哈希值。

## What Changes

分五步，每步产出的都是可核的东西：

1. **新运行时自带读写位置**。新增一套由 CodeGen 自己拥有的读写对象（写：只追加；读：字节起点 + 长度 + 已读位置，位置是显式参数）。生成物不再构造 `cacore::serializer` / `deserializer`。完成后，"容器里装结构体"不再需要拒绝：生成代码可以把当前读到的位置交给另一个类型的生成代码，由它接着往下读。写出的字节与现在保持逐字节一致，以便迁移期用逐字节比对验收。
2. **生成物脱离旧头**。生成物引用的五个旧符号全部换成新运行时的等价物（容器判据、哈希钩子、特化检测）。这一步做完，生成物与三份旧头解耦，旧头仍可为旧调用点服务。
3. **新哈希在旧调用点生效**。把 `Hasher.h` 第 86–96 行的分支顺序调换（"有自定义特化"优先于"平凡可拷贝"），使生成的特化能在 `HashObj<...>` / `aggregateHasher` 的既有调用点生效，**不改 `HashObj<...>` 的那 60 处调用点**。
4. **工具补齐**：支持命名空间作用域之外的嵌套类型标注；命令行类型名统一按最后一个非限定名解析（并在文档里写明）；给 D3D12 模块补上缺失的 include 目录，使其头文件可被解析。
5. **按模块迁移并删除旧头**。照 `GPUBufferDescriptor` 已跑通的套路逐类型搬（头里只留新标记，老宏挪到消费方 .cpp 让 ABI 守卫继续生效），最后把 `NameHash` / `PathHash` / `HashObj` 等宿主类型搬进新头，删除 `Reflection.h` / `Serialization.h` / `Hasher.h`。

**完成条件（可核、能失败）**：全仓 `git grep` 搜不到生成物或引擎代码引用 `cacore::serializer`、`cacore::deserializer`、`cacore::batch_deserializer`、`cacore::aggregateHasher`、`cacore::custom_hash_trait`、`careflection::containerStates`、`careflection::managed_wrapper_traits`、`CATypeDescriptor`、`CA_REFLECTION` 这些符号，且三份旧头可以从仓库删除而构建仍然通过。

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
- **删除**：`CACore/header/Reflection.h`、`Serialization.h`、`Hasher.h`（完成条件达成后），`NameHash` / `PathHash` / `HashObj` 等宿主类型迁到新头。
- **风险**：哈希值语义变化（整块字节 → 按字段）会影响所有用旧哈希值当键的内存字典 —— 今天没有任何哈希值写到磁盘，进程内会按新规则自洽重算；`HashObj<...>` 的相等比较是否也要改走生成特化的 `equal`，需要在设计阶段定清（否则会出现"哈希按字段、比较按字节"的错配）。
