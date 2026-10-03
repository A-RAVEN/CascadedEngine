> 构建只走 `python build.py`（项目唯一合法入口）。
> **前置**：`codegen-container-semantics` 已归档（容器识别、规范顺序、六条拒绝路径、退出码 12–17 都在）。本 change 的完成条件是 design 里写的那句：三份旧头不再被任何生成物或引擎代码引用，可以删除而构建通过。

## 1. 新运行时：自带游标的读写

- [ ] 1.1 新增 CodeGen 自有的读对象：持有字节起点、长度与已读位置，位置以显式参数在调用链上传递（读取入口接收缓冲区 + 位置引用）
  **怎么算做完**：新读对象能读回旧运行时写出的载荷，逐字段一致；**改之前**：在实现之前，"在流中间调用另一个类型的读代码"这件事无法表达（旧读对象的偏移是私有且无取值方法）
- [ ] 1.2 新增 CodeGen 自有的写对象：只持有缓冲区、只往后追加，无位置状态
  **怎么算做完**：同一对象用新写对象与旧 `cacore::serializer` 各写一遍，字节逐字节相同
- [ ] 1.3 写清标量与字符串的字节契约（整数宽度与字节序、`bool`、枚举底层类型、字符串先个数再字节、浮点按位），并写进 `Tools/CodeGen/README.md`
  **怎么算做完**：`git grep -n -F "<契约原文>" -- Tools/CodeGen/README.md` 命中（带文件限定）；`verify_readme.py` 0 处不符
- [ ] 1.4 读对象在越界时失败而不是读越界内存
  **怎么算做完**：喂一个被截断的载荷，读到边界时失败且可被调用方观察到；**改之前**：没有长度校验

## 2. 新运行时：哈希与容器判据

- [ ] 2.1 新运行时提供哈希对象（同样的 `hash_raw` / `hash(值)` / `getHash` 形状）与自有的特化检测
  **怎么算做完**：生成物改用新对象后，既有哈希用例读数不变（`CoreTests.exe` 退出码 0）
- [ ] 2.2 新运行时自有容器判据（有 `size()` 与 `value_type`，或 C 数组按 extent），与字段表的容器标志同口径
  **怎么算做完**：编译期断言"表里带 `Container` 标志 ⇔ 新判据为真"，对夹具八个成员逐个成立；对照组成员为假
- [ ] 2.3 容器元素的哈希规则（无序容器先排规范顺序、元素显式调用自身特化、浮点先规范化）迁移到新运行时
  **怎么算做完**：`CodeGen nested container + element hash PASSED` 与 `CodeGen container serialization PASSED` 两条用例保持通过；`CoreTests.exe` 退出码 0

## 3. 生成物切到新运行时

- [ ] 3.1 序列化发射器改为发新写/读对象的入口（缓冲区 + 显式位置），不再构造 `cacore::serializer` / `deserializer`
  **怎么算做完**：生成物里 `git grep -n -F "::cacore::serializer" -- <生成物目录>` 零命中；同一对象的字节与改动前逐字节相同（对拍）
- [ ] 3.2 哈希发射器改为发新运行时的特化模板与容器判据
  **怎么算做完**：生成物里 `cacore::custom_hash_trait` / `cacore::has_custom_hash_func` / `careflection::containerStates` 三个符号零命中（带文件限定）
- [ ] 3.3 五个旧符号的零引用判据固化成一条可跑的命令（脚本或测试），纳入构建
  **怎么算做完**：该判据在 3.1/3.2 完成前为红（现在每个符号都有命中），完成后为绿

## 4. 哈希接进既有调用点

- [ ] 4.1 调换 `Hasher.h` 的分派顺序，让"有自定义特化"优先于"平凡可拷贝"
  **怎么算做完**：拿一个平凡可拷贝的探针类型配生成的特化，断言"旧入口算出的哈希 == 按字段算的哈希"；**改之前**：两者不等（整块字节 vs 按字段）
- [ ] 4.2 验证四个既有特化的行为不变
  **怎么算做完**：`HashObj<...>` / `NameHash` / `PathHash` / `ComPtr<ID3DBlob>` 各取一值求哈希，结果与改动前一致；**改之前**：若哪个宿主类型其实是平凡可拷贝，这里就会红 —— 那时必须先处理它再继续
- [ ] 4.3 让相等判断与哈希同源（生成特化的 `equal` 在既有比较点生效）
  **怎么算做完**：两个内容相同但 padding / 内层插入顺序不同的对象，"相等为真"与"哈希相同"同时成立；只改哈希不改比较时该判据为红
- [ ] 4.4 写一条"填充字节不同不影响哈希"的用例（防止退回整块字节）
  **怎么算做完**：把对象里两个字段之间的填充字节改成不同值，哈希不变；**改之前**：走整块字节时哈希会变
- [ ] 4.5 设计里"`HashObj` 的比较走哪条路"这一条按实测结论落定，并把结论写进 design
  **怎么算做完**：design 里那段有对应的读数引用（哪个调用点、什么命令、什么输出）

## 5. 定长数组（无 `clear()`）

- [ ] 5.1 读取路径对定长数组按位置覆写，不调用 `clear()`
  **怎么算做完**：含 `castl::array<T, N>` 的类型在真实目标里实例化读路径并编译通过；**改之前**：生成物调用 `c.clear()`，而 `castl::array` 只是 `eastl::array` / `std::array` 的别名（`CASTL/CAArray.h` 七行、无 `clear`），实例化即编译失败
- [ ] 5.2 读之前校验载荷长度与 extent 一致
  **怎么算做完**：喂一个长度与 extent 不符的载荷，读失败且报出两者长度；**改之前**：没有这条校验
- [ ] 5.3 撤除"没有 `clear()` 就拒绝"这条路径，并同步 README 与 spec
  **怎么算做完**：`CPipelineStateObject` 一族（含 `castl::array`）三个发射器都为 0

## 6. 工具：嵌套类型、类型名口径、D3D12 环境

- [ ] 6.1 标记能标注嵌套类型（`DepthStencilStates::StencilStates`、`cahash::sha256_hash::result_type`）
  **怎么算做完**：两个类型三个发射器全为 0；**改之前**：现状是 4（类型未标注，附命令与 output）
- [ ] 6.2 类型名解析口径统一并写进 README（工具按最后一个非限定名查找；命令行与清单怎么写，文档里给可跑示例）
  **怎么算做完**：README 里的命令原样跑，全部成功；`verify_readme.py` 0 处不符
- [ ] 6.3 补齐 `D3D12RenderBackend` 的推导环境
  **怎么算做完**：对 `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h` 请求反射，退出码 0；**改之前**：现状是 6，附 `directx/d3dx12.h` 找不到的原文

## 7. 两类形状的规则做完

- [ ] 7.1 私有成员类型的 friend 入口：定义与老系统 `CA_PRIVATE_REFLECTION` 等价的写法，工具认它并出码
  **怎么算做完**：加一行后出码成功、不加仍拒绝（退出码 5）；两个方向都有读数
- [ ] 7.2 指针 / 托管指针成员：保持拒绝，并在 README 里写明替代做法（值语义结构 + 句柄写名字或索引）
  **怎么算做完**：`git grep -n -F "<替代做法原文>" -- Tools/CodeGen/README.md` 命中；对含指针成员的夹具仍非零退出且点名成员
- [ ] 7.3 把"哪些形状必须拒绝、哪些必须支持"整理成一张表，写进 README
  **怎么算做完**：表里每一行都有对应的探针读数（退出码 0 或非零），逐条对照

## 8. 按类型迁移

- [ ] 8.1 迁 `CPipelineStateObject` 一族的五个类型（无容器/指针，含 `castl::array` 的两个在 5.x 之后）
  **怎么算做完**：三个发射器为 0、生成物入库、真实目标 `Rendering` 的消费单元编译并断言通过、新旧字节对拍一致
- [ ] 8.2 迁 `cahash::sha256_hash::result_type`（`ShaderLibrary` 的键类型），解决它的全序问题
  **怎么算做完**：该类型三个发射器为 0；**改之前**：现状 `ShaderLibrary` 序列化退出码 17 并点名 `m_ShaderPrograms`（附原文）
- [ ] 8.3 迁 `ShaderLibrary`（VulkanRenderBackendNew），四个无序映射全部可出码
  **怎么算做完**：三个发射器为 0；生成物在该模块的真实编译单元里被 include 并断言
- [ ] 8.4 迁 `ShaderLibrary`（D3D12RenderBackend），依赖 6.3
  **怎么算做完**：三个发射器为 0（在 6.3 之前是 6）
- [ ] 8.5 迁 `ImageHandle` / `BufferHandle`，依赖 7.1
  **怎么算做完**：三个发射器为 0；**改之前**：现状三个都是 5（成员非 public）
- [ ] 8.6 处理含指针 / 句柄成员的类型（老后端 `VulkanPipelineObject` 一类）：按 7.2 的替代做法改结构或显式排除，并在 design 里记下结论
  **怎么算做完**：该类要么三个发射器为 0，要么在 design 的已知边界里有一条带实测读数的排除理由

## 9. 删除旧头与收尾

- [ ] 9.1 把 `NameHash` / `PathHash` / `HashObj` 等宿主类型从 `Hasher.h` 搬到新头，更新全部 include 点
  **怎么算做完**：全量构建通过；`git grep -n "#include \"Hasher.h\""` 的命中点逐个改到新头，零遗漏
- [ ] 9.2 把旧哈希驱动（`aggregateHasher` 等）的 11 处调用点与 `managed_wrapper_traits` 的 15 处使用点逐条判定并迁移
  **怎么算做完**：每条都有"改哪儿、依据什么"的记录；改完这些符号在全仓零引用
- [ ] 9.3 删除 `Reflection.h` / `Serialization.h` / `Hasher.h`
  **怎么算做完**：删除后 `python build.py --config Debug` → `BUILD SUCCESSFUL`、`CoreTests.exe` 退出码 0；**改之前**：在本 change 开始时删这三份头必然构建失败（附第一次失败输出）
- [ ] 9.4 全仓符号零引用判据（完成条件）
  **怎么算做完**：`git grep` 逐符号零命中：`cacore::serializer`、`cacore::deserializer`、`batch_deserializer`、`aggregateHasher`、`custom_hash_trait`、`careflection::containerStates`、`managed_wrapper_traits`、`CATypeDescriptor`、`CA_REFLECTION`（排除归档目录）
- [ ] 9.5 对外文档收尾：新运行时的能力边界、接口、退出码变化（12 撤除）、迁移做法
  **怎么算做完**：README 里每条命令原样可跑；`verify_readme.py` 0 处不符

## 10. 测试与判据（本 change 必须新增的）

- [ ] 10.1 新增 `Test/CoreTests/CodeGenRuntimeTest.cpp` 并接进 `CoreTests` 主流程：读回旧运行时写出的载荷逐字段一致；截断载荷在边界失败而不是读越界；定长数组的长度与 extent 不符时失败；把两个字段之间的填充字节改成不同值后哈希不变；"相等为真"与"哈希相同"同时成立；无序容器插入顺序不同时字节与哈希都相同
  **怎么算做完**：`CoreTests.exe` 退出码 0 且该用例的每一条断言都有独立读数；**改之前**：新读写实现之前，`截断载荷` 与 `填充字节` 两条必然失败（前者无长度校验、后者走整块字节）
- [ ] 10.2 对每一种格式规则各加一条"新旧两套写同一对象、字节逐字节相同"的对拍用例（标量、字符串、无序容器、有序容器、定长数组、嵌套结构体各一个夹具）
  **怎么算做完**：六条对拍全绿；**改之前**：新读写与旧运行时在任一规则上不一致时，对应那条变红（实现前它们无从比较，故先写出旧侧的期望字节）
- [ ] 10.3 新增 `Tools/CodeGen/check_legacy_symbols.py`：逐符号搜 `cacore::serializer` / `cacore::deserializer` / `batch_deserializer` / `aggregateHasher` / `custom_hash_trait` / `careflection::containerStates` / `managed_wrapper_traits` / `CATypeDescriptor` / `CA_REFLECTION`，列出命中文件与行号，命中即退出码 1
  **怎么算做完**：本 change 开始时跑它为红（现状每个符号都有命中，附输出）；3.x、9.x 做完后为绿；把它接进 `build.py` 的 codegen 段，使漏迁移会被构建挡住
- [ ] 10.4 每个迁移类型配一个真实目标里的消费单元断言（照 `TestContainerAssertions.cpp` / `TestWrapperAssertions.cpp` 的做法：字段标志、元素描述、偏移、ABI 守卫）
  **怎么算做完**：删掉入库生成物后该目标编译失败（红），恢复后构建成功（绿）；断言本身在字段表被改错时变红
- [ ] 10.5 拒绝形状的读数逐条留档：退出码 + stderr 点名成员；退出码 12 撤除后，同一个夹具改为"出码成功 + 往返一致"的读数
  **怎么算做完**：每条都有改动前与改动后两个读数，缺一不算完成
- [ ] 10.6 一条"生成物被真实目标消费"的判据（沿用既有做法：删掉入库生成物即构建失败，并给出加 `--commit-generated` 的提示）
  **怎么算做完**：实测删一份入库生成物 → 构建失败并打印提示（附原文）；恢复后构建成功
