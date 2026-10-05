## Context

现状（都是本仓实测，不是推断）：

- 生成的序列化代码对每个成员写 `s.serialize(obj.x)` / `d.deserialize(obj.x)`，`s` / `d` 是旧运行时的 `cacore::serializer` / `cacore::deserializer`；只有容器那一层由生成物自己写。
- 生成物引用的旧符号共五个：`cacore::serializer`、`cacore::deserializer`（`Serialization.h`）、`careflection::containerStates`（`Reflection.h`）、`cacore::custom_hash_trait`、`cacore::has_custom_hash_func`（`Hasher.h`）。
- 读游标（`deserializer::m_Offset`）与写缓冲区（`serializer::buffer`）都是私有成员，类里没有取值方法；生成物因此不能在流的中间调用另一个类型的生成代码 —— "容器里装结构体"只能拒绝（退出码 12）。
- `Hasher.h` 第 86 行的分派顺序是"平凡可拷贝优先"，生成物提供的 `custom_hash_trait<T>` 特化对纯 POD 类型永远轮不到（不报错，静默不生效）。
- 四个既有特化（`HashObj`、`NameHash`、`PathHash`、`ComPtr<ID3DBlob>`）对应的类型都不是平凡可拷贝：`HashObj` 第 199 行、`NameHash` 第 523 行、`PathHash` 第 364 行都有自己写的拷贝构造，`ComPtr` 有析构。
- 迁移套路已跑通一例：`GPUBufferDescriptor` 头里只放 `CA_REFLECT(serializable)`，老宏留在消费方 `.cpp`（`GPUBufferDescriptorReflection.cpp` 第 19 行），ABI 守卫继续生效。
- 工具侧空白：标记通道只服务命名空间作用域的类型（嵌套类型退出码 4）；D3D12 模块推导出的环境缺 `directx/d3dx12.h`（退出码 6）；命令行按最后一个非限定名查找类型，而文档里到处写全名。

## Goals / Non-Goals

**Goals:**

- 生成物自带一套读写与哈希设施，**不再引用**那三份旧头里的任何符号。
- 落盘字节与现在**逐字节一致**，使每一步迁移都能用"新旧两套写同一对象、比字节"来验收。
- "容器里装结构体"这类递归形状由生成代码支持，撤除退出码 12 的拒绝口。
- 让生成的新哈希在既有调用点生效，且不改那 70 处 `HashObj<...>` 调用点（实测计数）。
- 工具能标注嵌套类型；D3D12 的推导环境补全。
- 终态可核，完成条件按 proposal 里那三条**同时**成立来判：① 生成物与生产代码对符号表零引用（扫描范围、排除清单、`命中行数 == 白名单行数` 都写死）；② 三份旧头在**文件系统**上不存在（改名 / `git rm --cached` / 搬进新头都不算）且构建与 `CoreTests` 用例数不少于开工前；③ 旧实现指纹零命中，且三份旧头是删除而不是改名搬迁。

**Non-Goals:**

- 不改旧运行时的**运行时语义**来迎合新系统（唯一例外是 `Hasher.h` 的两行分派顺序）。工具自身的旧通道、文档示例、测试夹具、引擎自身类型源码的改动属于 D8 / D9 的处置范围，不算改旧运行时语义。
- 不支持按位存储容器（`vector<bool>`），也不为重复键容器定义二级排序 —— 这两条保持拒绝。
- 不迁移不在当前构建内的模块：`CAResource`、旧 `VulkanRenderBackend`、`Test/VulkanRendererBackendTester`（实测各 0 个编译单元），因此完成条件的扫描范围也把它们排除在外（排除清单写死在脚本里，不是"看着办"）。
- 不保证新旧哈希值相同（这是本 change 的目的之一：按字段算，避免 padding 与 `-0.0`）。

## Decisions

### D1 新运行时自带游标，读写都是显式参数

新增一对读写对象：写对象只持有缓冲区、只会往后追加（无位置状态）；读对象持有字节起点、长度与"已读位置"，位置作为显式参数从一个类型的读代码传到另一个类型的读代码。生成物的序列化入口改为接收缓冲区（写）或缓冲区加位置引用（读），不再构造旧的 `serializer` / `deserializer`。

**理由**：递归调用别的类型的生成代码只需要"当前位置"，而这正是旧类私有掉的唯一信息。**验收**：同一对象用新旧两套各写一遍，字节必须相同（迁移期这条判据能失败）。

### D2 生成物与旧头解耦的替代物清单

| 旧符号 | 新归属 |
|---|---|
| `cacore::serializer` / `deserializer` | 新运行时的写对象 / 读对象（D1） |
| `careflection::containerStates` | 新运行时自己的容器判据（有 `size()` 与 `value_type`，或 C 数组按 extent） |
| `cacore::custom_hash_trait` | 新运行时的哈希特化模板（同名同形，只是宿主换到新头） |
| `cacore::has_custom_hash_func` | 新运行时自己的特化检测 |

**理由**：这五个是生成物与旧头之间仅有的连接点，逐个换掉即可解耦；换完旧头仍能为旧调用点服务，不需要"某天一起切"。

### D3 哈希分派顺序调换（`Hasher.h` 两行）

把"有自定义特化"提到"平凡可拷贝"之前，使生成的特化在 `HashObj<...>` / `aggregateHasher` 既有调用点生效。

**理由与安全性**：构建内共**五个**既有特化，逐条核对如下（每个宿主都在它自己的编译单元里加一条 `static_assert(!std::is_trivially_copyable_v<宿主>)`，判据写成代码而不是叙述）：

| 特化所在 | 宿主 | 为什么不是平凡可拷贝 |
|---|---|---|
| `Hasher.h` | `HashObj<...>` | 自己写了拷贝构造 `HashObj(HashObj const& hashObj) : m_Object(hashObj.m_Object)` 与拷贝赋值 `constexpr HashObj& operator=(HashObj const& other)` |
| `Hasher.h` | `NameHash` | 自己写了拷贝构造 `constexpr NameHash(NameHash const& nameHash) : m_Name(nameHash.m_Name)` |
| `Hasher.h` | `PathHash` | 自己写了拷贝构造 `constexpr PathHash(PathHash const& pathHash) : m_Name(pathHash.m_Name)` |
| `D3D12RenderBackend/private/D3D12Includes.h` | `ComPtr<ID3DBlob>` | 有析构（Release） |
| `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h` | `graphics_backend::VulkanShaderResourceSet` | 成员含 `castl::unordered_map` |

（第六处 `custom_hash_trait<resource_management::ShaderResrouce>` 在 `Test/VulkanRendererBackendTester`，该模块 0 个编译单元，不在本 change 范围。）

所以调换不改动任何既有哈希值。**会变的**是迁移过来的 POD 类型：从"整块字节（含 padding）"变成"按字段" —— 这正是目的；今天没有任何哈希值写到磁盘，进程内字典会自洽重算。

**4.2 的读数（命令原样可跑，输出逐字照抄）**：

- `git grep -n -F "struct custom_hash_trait" -- "*.h" "*.cpp"` —— 引擎手写的特化 5 处：`CACore/header/Hasher.h`（`HashObj<ObjType, CompareMode, hashAlg>` / `NameHash` / `PathHash`）、`D3D12RenderBackend/private/D3D12Includes.h`（`ComPtr<ID3DBlob>`）、`VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h`（`graphics_backend::VulkanShaderResourceSet`）。同一条命令还会命中生成物与工具自己（都是本 change 新加的，不算"既有"）。
- `git grep -n -F "dispatch_branch_unchanged<" -- "*.h" "*.cpp"` —— 5 条编译期断言，各自钉在宿主所在的编译单元：`Hasher.h` 里 `NameHash` / `PathHash` / `HashObj<int>` 三行、`D3D12Includes.h` 里 `ComPtr<ID3DBlob>` 一行、`VulkanGraphExecutor.h` 里 `VulkanShaderResourceSet` 一行。判据不是"与调换前相等"这种复现不了的说法：`dispatch_branch_unchanged<宿主>()` 把"调换前选中哪条分支"（`trivial ? 1 : (custom ? 2 : 0)`）与"调换后选中哪条分支"（`custom ? 2 : (trivial ? 1 : 0)`）各算一遍再比，相等才编译得过。
- `out/build/x64-debug/bin/CoreTests.exe` 打印的三个既有宿主的哈希值（调换后实测，供以后对照）：
  `CodeGen legacy hash host NameHash     = 11718336935620011197`
  `CodeGen legacy hash host PathHash     = 12386457803575074623`
  `CodeGen legacy hash host HashObj<int> = 7869321708915449410`
- 分派顺序调换确实生效了：`CodeGen legacy hash entry uses the generated trait = 1 (legacy=15402126173295036644 fieldwise=15402126173295036644)` —— 走旧入口算出来的值与按字段算出来的值相等。

### D4 相等比较一并接入

`HashObj<...>` 的比较也必须走生成特化的 `equal`，否则会出现"哈希按字段、比较按字节"的错配（同一内容被判不等、或反之）。

**理由**：只改哈希不改比较，等于把一个更隐蔽的不一致引进缓存键。**验收**：构造"内容相同但 padding 或插入顺序不同"的两个对象，断言"相等判断为真"与"哈希相同"同时成立。

**4.3 的读数（命令原样可跑，输出逐字照抄）**：

- `git grep -n -F "has_custom_hash_func<" -- "*.cpp"` —— 两个真实既有调用点各带编译期断言：`D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.cpp`（`CPipelineStateObject` 一行、`RasterizerStates` 一行）、`D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp`（`CPipelineStateObject` 一行、`ColorAttachmentsBlendStates` 一行）。断言失败的状态就是"既有哈希入口看不到生成的特化、退回整块字节"。
- 运行期（`CoreTests.exe`）：`CPipelineStateObject hash ignores padding = 1 (fields equal = 1, overall bytes differ = 1, HashObj equal = 1, control differs = 1)` —— 两个对象字段全同、整体字节不同（填充不同），`HashObj` 判等为真且哈希相同；对照组（字段不同）为假。
- 填充字节那条的独立读数：`CodeGen hash ignores padding = 1 (padding differs = 1, fields equal = 1, raw bytes differ = 1)`。

### D5 嵌套类型的标注与类型名解析

标记要能标注命名空间作用域之外的嵌套类型（`DepthStencilStates::StencilStates`、`cahash::sha256_hash::result_type` —— 两者都是引擎里真实存在、已被老宏标注的类型）；命令行与清单里的类型名，工具按**最后一个非限定名**解析这一口径写进 `Tools/CodeGen/README.md`。

**理由**：现状是"标不上"（退出码 4）与"文档写了全名但工具只认短名"两件事同时存在，任一条都会让迁移在具体类型上卡住。

### D6 定长数组（无 `clear()`）不再拒绝

`castl::array` 是 `eastl::array` / `std::array` 的别名（`CASTL/CAArray.h` 只有七行，没有 `clear`），而生成的读取路径以 `c.clear()` 开头 —— 消费方一实例化就编不过。改为：按位置逐个覆写，并在读之前校验载荷里的长度与数组 extent 一致。

**理由**：这属于新运行时自己的格式规则，不该靠"拒绝"把 `CPipelineStateObject` 这类真实类型挡在外面。

### D7 迁移形态：头里新标记 + 消费方 .cpp 里老宏

沿用 `GPUBufferDescriptor` 的形态。**同一文件里两者并存是不允许的**：实测工具会走只支持反射的文本通道，序列化与哈希一律退出码 8。

### D8 私有成员与指针成员两类形状

- **私有成员的类型**（`ImageHandle` / `BufferHandle`、`ShaderBindingBuilder` 一族）：新系统需要一个与老系统 `CA_PRIVATE_REFLECTION` 等价的 friend 入口，由类型自己加一行；不加就保持拒绝（退出码 5）。
- **含指针 / 托管指针成员的类型**（老后端 `VulkanPipelineObject`）：生成期拒绝保持不变，迁移方式是把要落盘的部分提取成一个值语义的数据结构（句柄写名字或索引 + 旁表），而不是让序列化器去写进程内地址。

**理由**：这两类靠"加个标记"迁不动，必须先把取舍写死，否则"完全替代"里会留一个静默丢数据的口子。

**已知边界：`graphics_backend::ShaderSetData` 排除在接入清单之外**（2026-10-04 实测记录；它不加标记、不生成代码，清单行与三份生成物都已删掉）。红态读数是工具自己报的那两行：

```
CodeGen 'ShaderSetData' --emit-serialization exit 15: CodeGen: refusing --emit-serialization for
'graphics_backend::ShaderSetData' — member 'reflectionData' is a raw pointer
('const ShaderCompilerSlang::ShaderReflectionData *'): the old runtime does not write pointer members,
so the stream would silently lose it, and writing the address itself means nothing after a restart.
```

排除理由（不是"看着不行就算了"）：`reflectionData` 是**非拥有**的反向指针（`result.reflectionData = &fileInfo->reflectionData;`，`D3D12RenderBackend/private/RenderBackend_D3D12.cpp`），落盘没有意义；而这个类型只是 `GetShaderCodes()` 的临时返回值，不是 `ShaderLibrary` 的成员，也没有被任何其它已标注类型的成员引用 —— 它本来就不需要生成序列化 / 哈希。**同类形状的普查（2026-10-04 实测）**：迁移过的 6 个头（`CPipelineStateObject.h`、`Compiler.h`、两个 `ShaderLibrary.h`、`TestContainer.h`、`TestWrapper.h`）里带标记的类型共 **59 个**，全部在接入清单里、三个发射器都是 0 —— 也就是说，除了被排除的 `ShaderSetData`，没有第二个带指针成员的已标注类型。任务里点名的 `VulkanPipelineObject` 只出现在旧后端 `VulkanRenderBackend`，而该模块在本构建里有 **0 个编译单元**（`compile_commands.json` 实测），按规格本就不在迁移范围内，所以它既不用改结构、也不需要排除读数。

**何时可以重新考虑**：等它真的要落盘时，按上面那条把可落盘的部分提成值语义结构（句柄写名字或索引 + 旁表）。

### D9 四类旧用的处置（不处置则完成条件不可能成立）

对抗验证数出来：`CA_REFLECTION` 在非归档区有 115 处，其中四类不属于"迁移引擎类型"，必须单独定：

| 旧用 | 现状读数 | 处置 |
|---|---|---|
| 工具自身：`CA_REFLECTION` 的文本通道与识别逻辑 | `Tools/CodeGen/CodeGenMain.cpp` 命中（识别宏名、只支持反射的通道、退出码 8） | 随完成条件一起删除；删除后 `--emit-*` 只剩新标记通道，`README` 的"三条通道"章节与退出码 8 同步改掉 |
| 文档与示例 | `Tools/CodeGen/README.md`、`examples/reflection_channel.h`、`TestExternal.h` | 随文档收尾任务一起改；示例改成新标记写法并实跑留输出 |
| 测试夹具 | `Test/CoreTests` 里 6 个文件（含 ABI 守卫用例 `CodeGenMetaTest.cpp`） | 逐个迁移到新标记；**用例数不许减少**（收尾判据里逐条列名对账） |
| 类模板描述符：`CACore/header/uenum.h` 的 `CA_REFLECTION_TEMPLATE(uenum::EnumFlags<TEnumClass>, m_mask)` | 尝试后**已删除**（2026-10-04 实测）：`git grep -F "CA_REFLECTION_TEMPLATE" -- CACore/header/uenum.h` 命中 0 行、全量构建 `BUILD SUCCESSFUL`、`CoreTests.exe` 9 条全过；`EnumFlags` 的用法实测 8 行 / 3 个文件（旧记录写的"39 处"不准） | **不需要保留项**：新系统用 `uenum.h` 里的 `wrapper_traits<uenum::EnumFlags<TEnumClass>>` 覆盖它，生成物里这类成员就是 `cgWriteValue(w, obj.shaderTypeFlags)`。于是符号表里 `CA_REFLECTION_TEMPLATE` 与 `CA_REFLECTION` 一样要求归零（剩下的命中只是 `Reflection.h` 的宏定义本身，随 9.3 消失） |
| 三个哈希键类型的去处：`NameHash` / `PathHash` / `HashObj`（它们与旧设施同住 `Hasher.h`） | 搬到新头 `CACore/header/HashKeys.h`（2026-10-04 落地）：`Hasher.h` 838 → 323 行，里面这三个类型的定义命中 0；30 个 include 点改道到新头，只剩旧式用例 `SerializerTest.cpp` 还引 `Hasher.h`（10.x 替换） | 新头**只** include `CodeGenHashFwd.h` + `CACore/CAHash.h` + CASTL 字符串 + `CALiteralString.h`：一开始写成 include `CodeGenHash.h`，那会把整套序列化运行时拖进每份被标注的头的 include 图，工具的每次解析都变重（实测那次全量构建的 codegen 段明显拉长）。特化只需要主模板的声明，Fwd 版正好够 |

**理由**：这四类不写清，"全仓零引用"要么不可达、要么靠删测试与删文档凑出来；写清之后每条都有归属，收尾判据才可能既严格又能达成。

## Risks / Trade-offs

- **字节格式对不上**：新读写若与旧规则有一处不一致，迁移期的对拍会红 —— 这是要的（宁可红在迁移期，不要红在落盘之后）。代价是新写读的实现必须严格照抄宽度与顺序约定，并在文档里写死。
- **哈希值变化**：所有以旧哈希值当键的内存字典都会重算。今天无落盘哈希，所以只在同一进程内自洽；若将来有跨进程或落盘键，需要版本号（本 change 不含）。
- **`NameHash` / `PathHash` / `HashObj` 搬迁**：这三个类型住在 `Hasher.h` 里，被约 20 个文件 include。搬进新头要改那批 include，虽然机械但面广，必须一次做完并跑全量构建。
- **递归读的越界风险**：游标改为显式参数后，"读多少"要自己管；读对象必须带长度并在越界时失败，而不是读越界内存。这条要在新运行时里做进实现，并配一个截断载荷的用例。
