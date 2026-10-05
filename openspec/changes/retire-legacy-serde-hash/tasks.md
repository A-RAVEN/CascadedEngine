> 构建只走 `python build.py`（项目唯一合法入口）。
> **前置**：`codegen-container-semantics` 已归档（容器识别、规范顺序、六条拒绝路径、退出码 12–17 都在）。本 change 的完成条件是 design 里写的那句：三份旧头不再被任何生成物或引擎代码引用，可以删除而构建通过。

## 执行纪律（每轮开工先读这一段，违反即本轮作废）

1. **一次只改一个类型，连它成员里用到的类型一起**：一个类型要能出码，它成员里出现的结构体（以及那些结构体的成员）都得先标注，就这一批、一次改完；还没轮到的类型不许提前标注。前 10 轮只勾了 22/46，其中第 5–9 轮 0 勾选 —— 原因就是 PSO 那一族、`Compiler.h` 里的 24 个结构体、两个后端的 `ShaderLibrary` 三批同时改，一处错整仓编译就红，整轮都在恢复绿色。
2. **先跑工具，再跑构建**：改完先用 `CodeGen.exe` 对目标头单独出码（秒级），确认三个发射器为 0，再跑 `python build.py`；全量构建一轮最多一次，不许当探针用。
3. **先量后写判据**：凡涉及字节 / 指针 / 句柄成员的条目，判据必须同时写出「老实现的读数」与「新实现的读数」两个数。只写"逐字节相同"而没先量过的判据作废 —— 8.4 原来那条就是这么错的（第 9 轮才量出旧 118 / 新 130）。
4. **勾选即就地**：一条做完立刻把那条的 `- [ ]` 改成 `- [x]`，再动下一条；勾选的唯一依据是它自己写的那句验收条件已经实测通过；严禁批量补勾。
5. **每轮收尾**：贴 `openspec instructions apply --change retire-legacy-serde-hash --json` 的 remaining 数**和**本轮"已勾 / 未勾"的表。remaining 大于 0 又没写出下面三类（真障碍，附命令与真实输出），本轮算违规。
6. **熔断**：连续两轮没有新增勾选，立刻停止改动，把卡住的那条当真障碍报出（附命令与原始输出），不许转去做别的、也不许写汇报充数。

## 1. 新运行时：自带游标的读写

- [x] 1.1 新增 CodeGen 自有的读对象：持有字节起点、长度与已读位置，位置以显式参数从一个类型的读代码传到另一个类型的读代码（读取入口接收缓冲区 + 位置引用）
  **怎么算做完**：新读对象能读回旧运行时写出的载荷，逐字段一致；**改之前**：在实现之前，"在流中间调用另一个类型的读代码"这件事无法表达（旧读对象的偏移是私有且无取值方法）
- [x] 1.2 新增 CodeGen 自有的写对象：只持有缓冲区、只往后追加，无位置状态
  **怎么算做完**：同一对象用新写对象与旧 `cacore::serializer` 各写一遍，字节逐字节相同
- [x] 1.3 写清标量与字符串的字节契约（整数宽度与字节序、`bool`、枚举底层类型、字符串先个数再字节、浮点按位），并写进 `Tools/CodeGen/README.md`
  **怎么算做完**：`git grep -n -F "<契约原文>" -- Tools/CodeGen/README.md` 命中（带文件限定）；`verify_readme.py` 0 处不符
- [x] 1.4 读对象在越界时失败而不是读越界内存
  **怎么算做完**：喂一个被截断的载荷，读到边界时失败且可被调用方观察到；**改之前**：没有长度校验

## 2. 新运行时：哈希与容器判据

- [x] 2.1 新运行时提供哈希对象（同样的 `hash_raw` / `hash(值)` / `getHash` 形状）与自有的特化检测
  **怎么算做完**：生成物改用新对象后，既有哈希用例读数不变（`CoreTests.exe` 退出码 0）；**改之前**：把新哈希对象从新头里拿掉，生成物所在的编译单元立刻编译失败（哈希用例现在直接调生成的特化，不经过旧驱动，所以"用例通过"本身不是新运行时的判据 —— 必须补这条编译期红态）
- [x] 2.2 新运行时自有容器判据（有 `size()` 与 `value_type`，或 C 数组按 extent），与字段表的容器标志同口径
  **怎么算做完**：编译期断言"表里带 `Container` 标志 ⇔ 新判据为真"，对夹具八个成员逐个成立；对照组成员为假；**改之前**：断言指向旧 `careflection::containerStates`，把旧头从 include 路径移走即编译失败
- [x] 2.3 容器元素的哈希规则（无序容器先排规范顺序、元素显式调用自身特化、浮点先规范化）迁移到新运行时
  **怎么算做完**：`CodeGen nested container + element hash PASSED` 与 `CodeGen container serialization PASSED` 两条用例保持通过、`CoreTests.exe` 退出码 0，**并且**生成物里三个旧钩子（`cacore::custom_hash_trait` / `cacore::has_custom_hash_func` / `careflection::containerStates`）零命中 —— 只贴用例通过不算做完（那两条用例现在直接调生成的特化，不经过旧钩子）

## 3. 生成物切到新运行时

- [x] 3.1 序列化发射器改为发新写/读对象的入口（缓冲区 + 显式位置），不再构造 `cacore::serializer` / `deserializer`
  **怎么算做完**：生成物里 `git grep -n -F "::cacore::serializer" -- out/build/x64-debug/codegen` 与 `::cacore::deserializer` 都零命中（打印"扫描 N 个文件、命中 0 行"，且 **N MUST NOT 为 0** —— N=0 就是扫到了空集），同一对象的字节与改动前逐字节相同（对拍）
  ⚠ **扫描目标已改（2026-10-05，`improve-build-time` 落地时同步）**：源码树里那三个入库目录（`Tools/CodeGen/generated`、`Interface/RenderInterface/generated`、`CACore/generated`）已经删掉，生成物只活在 `out/build/<预设>/codegen/<模块>/`。原来那句"**不许**用 `out/build/.../codegen` 当扫描目标"是针对旧布局说的（那时它被 gitignore、工具静默不生成时恰好是空的 → 恒真）；现在扫它就是扫真东西，非空由 **N ≠ 0** 兜底：`check_legacy_symbols.py` 找不到任何生成物目录时直接以退出码 2 报错，不会把空集当"0 命中"。
- [x] 3.2 哈希发射器改为发新运行时的特化模板与容器判据
  **怎么算做完**：生成物里 `cacore::custom_hash_trait` / `cacore::has_custom_hash_func` / `careflection::containerStates` 三个符号零命中（限定到**构建目录里的生成物** `out/build/<预设>/codegen/<模块>/`）
- [x] 3.3 撤除"元素是内含无序容器的结构体"这条拒绝（`elementUnstableInside` 守卫与 `return 12`），递归读写接手这个形状
  **怎么算做完**：10.6 的那个夹具在改动前是 `exit 12` + stderr 点名成员 / 元素类型 / 内层无序容器，改动后 `exit 0` 且往返一致、两个只有内层插入顺序不同的对象字节逐字节相同；只改发射器入口、守卫还留着不算做完
- [ ] 3.4 把三条收尾判据固化成一条可跑的命令 `Tools/CodeGen/check_legacy_symbols.py`：① 逐符号扫描（**构建目录里的生成物** + 引擎源码后缀白名单），打印"扫描 N 个文件、命中 0 行"；② 文件系统枚举确认三份旧头不存在；③ 旧实现指纹（`visit_members`、`CATypeMemberDesc`、`containerInfo`、`SubmitAndWait`、`Object must be trivially copyable`、`hash_one`、`managed_pointer_traits`）零命中
  **怎么算做完**：脚本自己写死扫描范围、排除清单（`openspec/`、`out/`、脚本自身）与那一行 ABI 守卫白名单，并按 `命中行数 == 白名单行数` 判定；**改之前**：三个符号各给出真实命中行数与扫描文件数（今天必红），脚本自身与归档目录不许被算成命中；把它接进 `build.py` 的 codegen 段，漏迁移由构建挡住
- [x] 3.5 修 `build.py` 的失败回显：日志里出现非 ASCII（编译器把中文 `static_assert` 消息原样带出）时，回显用的 `print()` 抛 `UnicodeEncodeError`，真实的编译错误被 Python 回溯顶掉，日志文件还留在盘上
  **怎么算做完**：标准输出与错误输出强制 UTF-8；两次读数 —— **改之前**实测 `UnicodeEncodeError: 'charmap' codec can't encode characters in position 2150-2169`（真实的 C2248 诊断没被打印出来），改之后同一条含中文的编译错误原样打印：`error C2338: static assertion failed: '中文错误消息探针：这条断言故意失败，用来验证 build.py 的日志回显'`

## 4. 哈希接进既有调用点

- [x] 4.1 调换 `Hasher.h` 的分派顺序，让"有自定义特化"优先于"平凡可拷贝"
  **怎么算做完**：拿一个平凡可拷贝的探针类型配生成的特化，断言"旧入口算出的哈希 == 按字段算的哈希"；**改之前**：两者不等（整块字节 vs 按字段）
- [x] 4.2 程序化枚举构建内**全部**既有哈希特化，逐个验证行为不变，并对每个宿主加编译期断言
  **怎么算做完**：用 `git grep -n -F "struct custom_hash_trait"` 枚举（当前实测 **5 个**：`HashObj` / `NameHash` / `PathHash` / `ComPtr<ID3DBlob>` / `graphics_backend::VulkanShaderResourceSet`），每个宿主所在编译单元加 `static_assert(!std::is_trivially_copyable_v<宿主>)`，并给出改动前后各取一值的哈希读数；**改之前**：断言写下去就该成立 —— 哪一个不成立，就说明调换顺序会改它的哈希值，必须先在 design 里改结论、再动 `Hasher.h`。清单写进 design 的 D3 表格，不许在 tasks 里另写一个数
- [x] 4.3 让相等判断与哈希同源（生成特化的 `equal` 在**既有比较点**生效）
  **怎么算做完**：点名两个真实调用点（例如 `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` 的 `HashObj<CPipelineStateObject>`、`Interface/RenderInterface/header/ShaderResourceHandle.h` 里以 `HashObj<...>` 为键的字典），在其所在编译单元断言"内容相同但 padding / 内层插入顺序不同的两个对象：相等为真且哈希相同"；**不许**只在 `Test/CoreTests/GeneratedHashContainerTest.cpp` 里直接调生成的特化就算完成 —— 那里今天就是绿的
- [x] 4.4 写一条"填充字节不同不影响哈希"的用例（防止退回整块字节）
  **怎么算做完**：把对象里两个字段之间的填充字节改成不同值，哈希不变；**改之前**：走整块字节时哈希会变
- [x] 4.5 把 4.2 / 4.3 的实测读数补齐到 design（哪一行、什么命令、什么输出），使 design 的 D3 / D4 结论可回溯
  **怎么算做完**：design 的 D3 表格与 D4 一段都指向具体命令与读数；它们不是验收判据（判据在 4.2 / 4.3），这条只做记账

## 5. 定长数组（无 `clear()`）

- [x] 5.1 读取路径对定长数组按位置覆写，不调用 `clear()`
  **怎么算做完**：含 `castl::array<T, N>` 的类型在真实目标里实例化读路径并编译通过；**改之前**：生成物调用 `c.clear()`，而 `castl::array` 只是 `eastl::array` / `std::array` 的别名（`CASTL/CAArray.h` 七行、无 `clear`），实例化即编译失败
- [x] 5.2 读之前校验载荷长度与 extent 一致
  **怎么算做完**：喂一个长度与 extent 不符的载荷，读失败且报出两者长度；**改之前**：没有这条校验
- [x] 5.3 撤除"没有 `clear()` 就拒绝"这条路径，并同步 README 与 spec
  **怎么算做完**：`CPipelineStateObject` 一族（含 `castl::array`）三个发射器都为 0

## 6. 工具：嵌套类型、类型名口径、D3D12 环境

- [x] 6.1 标记能标注嵌套类型（`DepthStencilStates::StencilStates`、`cahash::sha256_hash::result_type`）
  **怎么算做完**：两个类型三个发射器全为 0；**改之前**：现状是 4（类型未标注，附命令与 output）
- [x] 6.2 类型名解析口径统一并写进 README（工具按最后一个非限定名查找；命令行与清单怎么写，文档里给可跑示例）
  **怎么算做完**：`git grep -n -F "<解析口径原文>" -- Tools/CodeGen/README.md` 命中（带文件限定），README 里的命令原样跑全部成功，`verify_readme.py` 0 处不符；只跑 `verify_readme.py`（今天就是 0 处不符）不算
- [x] 6.3 补齐 `D3D12RenderBackend` 的推导环境
  **怎么算做完**：对 `D3D12RenderBackend/private/D3D12Includes.h`（该文件内部没有老宏标注，会真正走 libclang 解析与诊断检查）请求反射，退出码 0；**改之前**：实测是 6，stderr 逐字含 `unresolved include (fatal): D3D12RenderBackend/private/D3D12Includes.h:11:10: fatal error: 'directx/d3dx12.h' file not found`。⚠ 不许拿 `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h` 当这条的红态：它自己在文件末尾带老宏标注，工具会走只支持反射的文本通道并直接 `exit 0`（已实测），拿它当判据等于恒真
- [x] 6.4 迁移 `CACore/header/uenum.h` 的类模板描述符 `CA_REFLECTION_TEMPLATE(uenum::EnumFlags<TEnumClass>, m_mask)`
  **怎么算做完**：先尝试把它迁到新系统（引擎里 `EnumFlags<` 有 39 处使用，迁移后这些用法必须照常编译与求哈希）；**判据能失败**：给出"改动前该模板描述符存在、改动后不再需要"的两个读数；若实测不可行（工具现在明确不支持类模板），在本 change 内交出实测输出与原因，并把它写进完成条件的**唯一保留项**（只允许 `CA_REFLECTION_TEMPLATE` 留在 `uenum.h`，非模板形态的 `CA_REFLECTION` 仍须为零），不许默默留着了事
  **已做（2026-10-04）：不是"保留"，是删掉了 —— 尝试结果是可行，那条"唯一保留项"用不上。** 符号表里 `CA_REFLECTION_TEMPLATE` 照常要求归零（剩下的命中只有 `Reflection.h` 里的宏定义本身，随 9.3 一起消失）。读数：
  - **改动前**：`git show HEAD:CACore/header/uenum.h` 里有 `CA_REFLECTION_TEMPLATE(uenum::EnumFlags<TEnumClass>, m_mask);` 一处使用（另有说明文字一处）。
  - **改动后**：`git grep -n -F "CA_REFLECTION_TEMPLATE" -- CACore/header/uenum.h` → **命中 0 行**；`python build.py --config Debug` → `BUILD SUCCESSFUL`（编译 63.6s，`uenum.h` 被广泛包含，所以这一轮重编了一批单元）；`CoreTests.exe` 退出码 0、9 条用例全 PASSED。
  - **用量的实际读数（修正条目里"39 处"这个旧数）**：`git grep -F "EnumFlags" -- "*.h" "*.cpp"`（排除 `openspec/` 与 `uenum.h` 自身）实测 **8 行、3 个文件**（`Interface/RenderInterface/header/Common.h` 6 行、`VulkanRenderBackend/private/ResourceUsageInfo.h` 1 行、`Interface/ShaderCompiler/header/Compiler.h` 1 行）。新系统覆盖它的方式是 `uenum.h` 里那条 `wrapper_traits<uenum::EnumFlags<TEnumClass>>`：生成物里这些成员就是 `cgWriteValue(w, obj.shaderTypeFlags)` 这样的调用，读写与哈希都按里面的掩码算。
- [x] 6.5 处置工具自身、README 与示例里的旧通道
  **已做（2026-10-04）**：读数逐条 ——
  - `git grep -n -F "CA_REFLECTION" -- Tools/CodeGen/CodeGenMain.cpp` → **0 行**（改前 **36 行**：识别宏名、`bindAnnotations` 的按名解析分支、`scanCA_REFLECTIONList`、只用成员名的发射分支、退出码 8，以及若干注释）；
  - `git grep -n -F "return 8;" -- Tools/CodeGen/CodeGenMain.cpp` → **0 处**（改前 1 处）；
  - `Tools/CodeGen/TestExternal.h` 与 `Tools/CodeGen/examples/reflection_channel.h` → 文件系统枚举**都不存在**（改前都存在）；
  - `Tools/CodeGen/README.md`：`reflection_channel` / `TestExternal` 命中 **0 行**（改前 5 行）；"三条通道"章节改成两条、退出码 8 那一行与示例 6 删掉（后面的示例重新编号）、退出码 4 与 5 的报错原文换成实测新文本；
  - 实跑：`python Tools/CodeGen/verify_readme.py` → **`README: 17 documented command(s) run, 0 mismatch(es)`**（改前 19 条，删掉的是旧通道那两条）；退出码 4/5 的新原文由工具当场跑出来照抄进文档。
  **怎么算做完**：`Tools/CodeGen/CodeGenMain.cpp` 里 `CA_REFLECTION` 的识别逻辑与只支持反射的文本通道随完成条件删除（删除后 `--emit-*` 只剩新标记通道）；`Tools/CodeGen/README.md` 的"三条通道"章节、退出码 8、`examples/reflection_channel.h`、`TestExternal.h` 同步改掉并实跑留输出；**改之前**：逐处给出命中行数（今天是红的）

## 7. 两类形状的规则做完

- [ ] 7.1 私有成员类型的 friend 入口：定义与老系统 `CA_PRIVATE_REFLECTION` 等价的写法，工具认它并出码
  **怎么算做完**：加一行后出码成功、不加仍拒绝（退出码 5）；两个方向都有读数
- [x] 7.2 指针 / 托管指针成员：保持拒绝，并在 README 里写明替代做法（值语义结构 + 句柄写名字或索引）
  **怎么算做完**：`git grep -n -F "<替代做法原文>" -- Tools/CodeGen/README.md` 命中；对含指针成员的夹具仍非零退出且点名成员
- [x] 7.3 把"哪些形状必须拒绝、哪些必须支持"整理成一张表，写进 README
  **怎么算做完**：表里每一行都有对应的探针读数（退出码 0 或非零），逐条对照

## 8. 按类型迁移

- [x] 8.1 迁 `CPipelineStateObject` 一族的五个类型（无容器/指针，含 `castl::array` 的两个在 5.x 之后）
  **怎么算做完**：三个发射器为 0、生成物落在构建目录（`out/build/<预设>/codegen/Interface_RenderInterface/`）、真实目标 `Rendering` 的消费单元编译并断言通过、新旧字节对拍一致
  ⚠ 措辞已改（2026-10-05）：原文写的是"生成物**入库**" —— 生成物不再进源码树，落到构建目录里了。
- [x] 8.2 迁 `cahash::sha256_hash::result_type`（`ShaderLibrary` 的键类型），解决它的全序问题
  **怎么算做完**：该类型三个发射器为 0；**改之前**：现状 `ShaderLibrary` 序列化退出码 17 并点名 `m_ShaderPrograms`（附原文）
- [x] 8.3 迁 `ShaderLibrary`（VulkanRenderBackendNew），四个无序映射全部可出码
  **怎么算做完**：三个发射器为 0；生成物在该模块的真实编译单元里被 include 并断言
- [ ] 8.4 迁 `ShaderLibrary`（D3D12RenderBackend），依赖 6.3
  **怎么算做完**：三个发射器为 0（在 6.3 之前是 6）；再补 10.4 的消费单元断言与新旧字节对拍，三样齐了才算（规格里"三样"没有"或"）
  **判据修订（2026-10-04，按 AGENTS §7 三条齐备改的，改前/改后原文都留在这里）**：
  - **改前那句**：「三样齐了才算（规格里"三样"没有"或"）」+ 规格里"新旧两套写同一对象、字节逐字节相同"。
  - **为什么改**：`ShaderLibrary` 的成员类型里（一层层展开之后）含 `ComPtr<ID3DBlob>` 成员，实测老路径**先判指针**（`managed_pointer_traits<ComPtr<T>>` 特化）→ 指针成员**一个字节都不写**，所以老字节里根本没有 blob 的内容。要求"逐字节相同"就等于要求新代码继续把数据丢掉 —— 这与本 change 的 7.2 / design D8（含指针成员不许静默丢数据）直接冲突。实测读数：空对象两边都是 32 字节且逐字节相同；含一个程序时**旧 118 / 新 130**，差的 12 字节正好是被老路径丢掉的 blob（`uint64 4` + `"ABCD"`）。
  - **改后（更严，不是放宽）**：对拍拆成两问，都要有读数 ——① **除指针 / 句柄成员外逐字节相同**（把两侧的指针成员置空后再比，逐字节）；② **指针 / 句柄成员往返一致**（写出去再读回来，blob 的字节数与内容都不丢）。老判据只问了"整体相同"，第②问是它没有的。
  - **溯源**：依据是 `tasks.md` 7.2「指针 / 托管指针成员：保持拒绝，并在 README 里写明替代做法」与 design 的 D8「不许静默丢数据」，以及本轮实测的 118 / 130 两个读数。
- [ ] 8.5 迁 `ImageHandle` / `BufferHandle`，依赖 7.1
  **怎么算做完**：三个发射器为 0；**改之前**：现状三个都是 5（成员非 public）；再补 10.4 的消费单元断言与新旧字节对拍
- [x] 8.6 处理含指针 / 句柄成员的类型（老后端 `VulkanPipelineObject` 一类）：按 7.2 的替代做法改结构或显式排除，并在 design 里记下结论
  **怎么算做完**：要么三个发射器为 0 且三样验收齐备；要么在 design 的已知边界里给出一条**带红态读数**的排除理由（同一类型在探针里被拒的退出码 + stderr 点名的那一行）。只写"记为已知边界"不算
- [ ] 8.7 `TResource`（`GeneralResources/header/CAResource/IResource.h`）的读写已经从 `batch_serializer` / `batch_deserializer` 换成生成代码，但**这条改动一个读数都没有**，补对拍
  **怎么算做完**：同一个对象，老 `cacore::batch_serializer` 与新 `cacore::codegen::CodeGenSerializer` 写出的字节**逐字节相同**（用一个把 `Write` 收进 `castl::vector` 的 `WBatch` 探针接管，不依赖文件系统）；再断言 `TResource::Serialize` 转发出去的那段与 `CodeGenSerializer` 直接写出的那段相同（它只该转发，不该自己加长度前缀之类的料）；把新写出的字节喂回新读侧，逐字段读回一致。三个读数都要贴。**必须在 9.3 删 `Serialization.h` 之前做完** —— 老侧的对照只有那之前才有。**改之前**：三个读数今天都是空的（这条改动是"读了代码觉得对"就留下的）

## 9. 删除旧头与收尾

- [ ] 9.1 把 `NameHash` / `PathHash` / `HashObj` 等宿主类型从 `Hasher.h` 搬到新头，更新全部 include 点
  **怎么算做完**：全量构建通过；`git grep -n -F "#include <Hasher.h>"`（尖括号是真实写法，引号写法今天 0 命中、永远绿，不许用它当判据）的命中点逐个改到新头，零遗漏；**改之前**：先贴出这条命令的命中行数与文件数（2026-10-04 实测 **32 行 / 32 个文件**，其中 1 行是 `tasks.md` 自己引用的这句命令，源码里 31 行；含 `CACore/header/uenum.h`。条目原来写的"40 行以上"是更早的读数，以这次实测为准）
  **已做（2026-10-04，等全量构建确认后勾）**：新头 `CACore/header/HashKeys.h` 545 行（`default_hashclass` / `has_std_hash` / `hash` / `hash_256` / `EHashObjCompareMode` / `HashObj` 101 行 / `PathHash` 162 行 / `NameHash` 151 行 + 末尾 63 行的 `wrapper_traits`、`custom_hash_trait` 六块特化）；`Hasher.h` **838 → 323 行**，里面 `struct HashObj` / `struct PathHash` / `struct NameHash` 命中 **0**；include 点：**30 个文件**改成 `#include <HashKeys.h>`，只剩 `Test/CoreTests/SerializerTest.cpp` 还留 `Hasher.h`（旧式测试，归 10.1/10.2 替换）——收尾读数是 `git grep -l -F "#include <Hasher.h>"` = 1，10.x 做完要变 0。**搬运过程出过一次事故并已修**（详见本轮报告）：先用"第一个 `};`"当块尾，把 `PathHash`/`NameHash` 切短、`Hasher.h` 被切碎（编译报 `Hasher.h(45): fatal error C1075`）；改用花括号配对重新切块、`Hasher.h` 用 `git cat-file blob HEAD:` 逐字节还原后重做，`python build.py --config Debug --no-codegen CACore` → `BUILD SUCCESSFUL`。
  **改之前的块边界（花括号配对实测）**：`PathHash` 361-522、`NameHash` 526-676、`HashObj` 214-314、特化块 776-838 —— 这四行是可复核的对照，说明块尾不是"第一个 `};`"。
  **新头的 include 要轻（2026-10-04 实测）**：`HashKeys.h` 一开始 include 的是 `CodeGenHash.h`，那会把整套序列化运行时（`CodeGenSerialization.h` + `CAHash` + CASTL 容器头）拖进 include 图；而 `Hasher.h` 被 30 个文件包含、又会被每份"被标注的头"间接收进来，于是工具的每次解析都变重（该轮全量构建的 codegen 段明显拉长）。改成 `CodeGenHashFwd.h`（只有 `custom_hash_trait` 主模板 + `has_custom_hash_func` 检测，外加它自带的 `CodeGenTraits.h`，正好够那六块特化用）。
- [ ] 9.2 把旧哈希驱动（`aggregateHasher` 等）与 `managed_wrapper_traits` 的每一处使用点逐条判定并迁移，判据是可跑断言而不是叙述
  **已做（2026-10-04，等全量构建 + CoreTests 绿后勾）**：旧头文件之外的命中逐符号清零 —— `aggregateHasher` 22 → **0**、`managed_wrapper_traits` 22 → **0**、`managed_pointer_traits` 12 → **0**、`hash_one` 8 → **0**、`containerStates` 23 → **0**、`visit_members` 17 → **0**（命令一律 `git grep -n -F <符号> -- "*.h" "*.cpp"`，排除 `openspec/` 与 `CACore/header/{Reflection,Serialization,Hasher}.h` 三份待删的旧头）。改动点：`D3D12Includes.h` 删掉旧侧 `custom_hash_trait<ComPtr<ID3DBlob>>` / `managed_wrapper_traits` / `managed_pointer_traits` 三块（新侧那份留着）；`ShaderImporter_D12.cpp` 的 `set_data` 改走 `cacore::codegen::wrapper_traits`；`CASTL/CASharedPtr.h` 与 `CAUniquePtr.h` 删掉旧特化与它们的 `<Serialization.h>` include；我给 D3D12 那两处加的断言从 `cacore::aggregateHasher<>` 改成 `cacore::codegen::hasher<>`；三个测试文件的同类替换与注释改写。整仓扫描器读数 **130 → 63 行**，剩下的是三份旧头自己的实现（随 9.3 消失）。
  **怎么算做完**：每个符号给出"改动前命中行数 → 改动后 0 行"的两次读数（命令与输出都贴）；"每条都有记录"这种自述不算验收
- [ ] 9.3 删除 `Reflection.h` / `Serialization.h` / `Hasher.h`
  **怎么算做完**：文件系统枚举确认三个路径不存在（不是 `git rm --cached`、不是改名、不是搬进新头）；随后 `python build.py --config Debug` → `BUILD SUCCESSFUL`、`CoreTests.exe` 退出码 0 且用例名逐条列出、总数不少于开工前；**改之前**：在本 change 开始时删这三份头必然构建失败（附第一次失败输出）
- [ ] 9.4 三条收尾判据全绿（完成条件本体）
  **怎么算做完**：`python Tools/CodeGen/check_legacy_symbols.py` 打印"扫描 N 个文件、命中 0 行"，且 **N MUST NOT 为 0**、并与上一次跑同一条命令的 N 相同（同一批产物下这个数必须稳定）；三份旧头文件系统枚举不存在；旧实现指纹零命中；**ABI 守卫白名单必须是 0 行**（2026-10-04 修正：原来打算给消费方 `.cpp` 的老宏留逐行白名单，但那个宏与 `CATypeDescriptor` 都定义在 `Reflection.h` 里，头一删就编译不过 —— 所以改成终态 0 行，那批断言改成与写死的期望值比，见 9.6）；`git log --diff-filter=R --name-status` 证明三份旧头是被删除而不是改名搬迁
- [ ] 9.5 对外文档收尾：新运行时的能力边界、接口、退出码变化（12 撤除、8 随旧通道删除）、迁移做法
  **怎么算做完**：README 里每条命令原样可跑；`verify_readme.py` 0 处不符；退出码表与 7.3 那张表逐行一致
- [ ] 9.6 把依赖老描述符的那批代码改掉（删旧头之前必须做完；2026-10-04 新增，起因见 proposal 的"唯一的豁免"修正）
  **怎么算做完**：下面每一处都给出"改动前 → 改动后"的读数，全部归零，且 `python build.py --config Debug` + `CoreTests.exe` 绿：
  - `CACore/header/CodeGenMeta.h` 的 `static_assert_abi_consistency<T>()`：删掉 `careflection::has_type_desc<T>` 那一支（只剩"生成的名字非空"这条检查）；
  - `Interface/RenderInterface/private/GPUBufferDescriptorReflection.cpp`：把"与 `::CATypeDescriptor<T>::member_count` 比"改成与写死的期望值比，并去掉 `#include <Reflection.h>` 与那行老宏；
  - `Test/CoreTests/CodeGenMetaTest.cpp` 与 `EmitReflectionTest.cpp`：同上，**用例数不许减少**；
  - `D3D12RenderBackend/private/ShaderLibrary/ShaderImporter_D12.cpp` 与 `VulkanRenderBackend/private/ShaderLibrary/ShaderImporter_Vulkan.cpp` 里那句调试打印 `CATypeDescriptor<ShaderLibrary>::member_count`：删掉；
  - 三个 `ShaderLibrary.h`（`VulkanRenderBackendNew` / `D3D12RenderBackend` / 旧 `VulkanRenderBackend`）里的 `friend struct CATypeDescriptor<...>;`：删掉。
  **改之前**：`git grep -n -E "has_type_desc|CATypeDescriptor" -- "*.h" "*.cpp"`（排除 `openspec/`）实测 **35 行、11 个文件** —— 其中 14 行在 `Reflection.h` 自己身上（随 9.3 一起消失），剩下 **21 行分布在 10 个文件**，就是这条要清掉的数。
  **已做（2026-10-04，等全量构建 + CoreTests 绿后勾）**：同一命令现在 **17 行 / 2 个文件** —— 只剩 `Reflection.h` 自己（14 行，随 9.3 删头消失）与旧式用例 `Test/CoreTests/SerializerTest.cpp`（3 行，归 10.1/10.2 替换），**9.6 点名的 10 个文件是 0 行**。具体：`CodeGenMeta.h` 的守卫去掉与老描述符比较那一支（只剩"生成的名字非空"）；`GPUBufferDescriptorReflection.cpp`、`CodeGenMetaTest.cpp`、`EmitReflectionTest.cpp` 的期望值改成写死的数（2 / 3 / 4）并去掉老宏与 `#include <Reflection.h>`（`CodeGenMeta.h` 那句 include 也删了）；两个 `ShaderImporter` 里打印 `CATypeDescriptor<ShaderLibrary>::member_count` 的两行删除；三个 `ShaderLibrary.h` 的 `friend struct CATypeDescriptor<...>;` 删除。

## 10. 测试与判据（本 change 必须新增的）

- [ ] 10.1 新增 `Test/CoreTests/CodeGenRuntimeTest.cpp` 并接进 `CoreTests` 主流程：读回旧运行时写出的载荷逐字段一致；截断载荷在边界失败而不是读越界；定长数组的长度与 extent 不符时失败；把两个字段之间的填充字节改成不同值后哈希不变；"相等为真"与"哈希相同"同时成立；无序容器插入顺序不同时字节与哈希都相同
  **怎么算做完**：`CoreTests.exe` 退出码 0 且该用例的每一条断言都有独立读数；**改之前**：新读写实现之前，`截断载荷` 与 `填充字节` 两条必然失败（前者无长度校验、后者走整块字节）
- [ ] 10.2 对每一种格式规则各加一条"新旧两套写同一对象、字节逐字节相同"的对拍用例（标量、字符串、无序容器、有序容器、定长数组、嵌套结构体各一个夹具）
  **怎么算做完**：六条对拍全绿；**改之前**：新读写与旧运行时在任一规则上不一致时，对应那条变红（实现前它们无从比较，故先写出旧侧的期望字节）
- [ ] 10.3 新增 `Tools/CodeGen/check_legacy_symbols.py`：脚本顶部写死一份符号表（`cacore::serializer` / `cacore::deserializer` / `cacore::batch_deserializer` / `cacore::aggregateHasher` / `cacore::custom_hash_trait` / `cacore::has_custom_hash_func` / `careflection::containerStates` / `careflection::managed_wrapper_traits` / `CATypeDescriptor` / `CA_REFLECTION`）、一份路径排除清单（`openspec/`、`out/`、脚本自身）与那一行 ABI 守卫白名单，列出命中文件与行号，命中数大于白名单数即退出码 1；另附 `--fingerprints` 模式扫旧实现指纹（`visit_members` / `CATypeMemberDesc` / `containerInfo` / `SubmitAndWait` / `Object must be trivially copyable` / `hash_one` / `managed_pointer_traits`）与 `--files-exist` 模式用文件系统枚举查三份旧头
  **怎么算做完**：本 change 开始时跑它为红并打印"扫描 N 个文件、命中 M 行"（三个数字都要贴）；3.x、9.x 做完后为绿且 N 不变；把它接进 `build.py` 的 codegen 段，使漏迁移会被构建挡住；脚本自己不许被算成命中（否则一接进构建就永久挡住）
- [ ] 10.4 每个迁移类型配一个真实目标里的消费单元断言（照 `TestContainerAssertions.cpp` / `TestWrapperAssertions.cpp` 的做法：字段标志、元素描述、偏移、ABI 守卫）
  **怎么算做完**：生成不出该类型时（工具非零退出，例如把某个类型从清单里去掉、或让它触发一条拒绝）构建失败并点名类型与退出码（红）；生成成功时该目标编译通过、断言通过（绿）。断言本身在字段表被改错时变红。
  ⚠ **判据已改（2026-10-05，`improve-build-time` 落地时同步）**：原文是"删掉**入库**生成物后该目标编译失败" —— 生成物已经不在源码树里了，而且删掉构建目录里那一份只会让生成段**重新生成它**（构建照常成功），所以那条判据在终态下再也红不了。改成上面这条"生成不出来即失败"，它仍然会红（`improve-build-time` 的实测红态：`CodeGen 'NoSuchAnnotatedType' --emit-all: 工具退出码 0，却没有写出这些产物：…` → `CODEGEN FAILED (1)`）。
- [ ] 10.5 拒绝形状的读数逐条留档：退出码 + stderr 点名成员、涉事类型与原因；退出码 12 撤除后，10.7 那个夹具改为"出码成功 + 往返一致"的读数
  **怎么算做完**：每条都有改动前与改动后两个读数，缺一不算完成
- [ ] 10.6 一条"生成物被真实目标消费"的判据：把某个被标注类型从接入清单里去掉（或让它触发一条拒绝）→ 构建**失败**并点名类型与退出码；加回去 → 构建成功
  **怎么算做完**：实测去掉一条清单条目（或让它拒绝）→ 构建失败并打印提示（附原文）；恢复后构建成功。
  ⚠ **判据已改（2026-10-05，`improve-build-time` 落地时同步）**：原文是"删掉入库生成物即构建失败，并给出加 `--commit-generated` 的提示" —— `--commit-generated` 这个开关**已经被删除**（传它会直接报错退出），生成物也不在源码树里了，所以那条判据在终态下只能红。改成上面这条，它仍然会红。
- [ ] 10.7 新增"容器里装结构体"的夹具：按被撤除的那条拒绝所描述的形状（`struct Inner { castl::unordered_map<unsigned,unsigned> m; }; struct Outer { castl::vector<Inner> items; };`）造一个真实夹具，进接入清单（生成物落构建目录，不再"入库"）、加消费单元断言
  **怎么算做完**：**改之前**：同一夹具请求序列化 → `exit 12` + stderr 点名成员、元素类型与内层无序容器；**改之后**：`exit 0`，往返内容一致，且两个只有内层插入顺序不同的对象字节逐字节相同。这个夹具是 3.3、10.1（往返那一条）与 10.5 共同的指称对象，先有它再谈撤除
