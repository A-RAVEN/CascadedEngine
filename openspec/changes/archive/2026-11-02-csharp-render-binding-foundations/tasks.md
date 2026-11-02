# csharp-render-binding-foundations 实施任务

用户故事：

- **US1**：后续渲染绑定开发者需要直接复用记录、枚举、标志位、名称、路径和字节数据，而不为每种组合手写一套绑定。
- **US2**：C# 调用方需要把跨接口返回的资源安全交给另一个接口，并获得明确的类型、所有权和失效行为。
- **US3**：绑定维护者需要可失败的回归判据，确保共用生成器和句柄表的修改不破坏现有窗口能力。

证据统一写入 `openspec/changes/csharp-render-binding-foundations/evidence/`。每条红态都必须来自缺陷仍存在时的真实命令；不得用预期失败文本代替运行输出，不得在仓库其他位置留下临时探针或日志。

## 1. 先建立夹具与红态判据

- [x] 1.1 [US1] 新增 `Tools/CodeGen/BindableShapes.RenderFoundation.h`，覆盖平坦/嵌套记录、`bool`、窄枚举、方法级与记录级 `EnumFlags`、静态工厂、非虚查询、独立 `NameHash` / `PathHash` / `HashObj<T>`、字节与标量/记录/文本/枚举/标志位/句柄混合参数；为 `HashObj<T>` 的记录接入 `CA_REFLECT(serializable)` 生成特化，在 `build.py` 的 `CODEGEN_ANNOTATED` 登记并让消费头包含对应生成物。更新 `Tools/CodeGen/BindableShapes.expected.txt` 和 `Tools/CodeGen/check_bindings.py` 登记对应 C ABI 与公开表面判据，并把夹具加入 `CODEGEN_BINDINGS`。｜**验收**：在修改生成器前运行 `python build.py --config Debug`，生成或托管编译必须实际失败并点名未支持的记录、枚举、标志位、包装值或公开签名，完整输出存为 `evidence/foundation-shapes-red.log`；不得通过保留 `CA_BIND_SKIP`、使用没有 `bool` / `uint16_t` 的夹具或删除新断言取得通过。
- [x] 1.2 [US2] 新增 `Tools/CodeGen/BindableShapes.RenderResource.h` 与 `Tools/CodeGen/BindableShapes.RenderOwner.h`，分头声明资源接口、发行接口、强/弱/借用返回和 `shared_ptr` 入参；新增 `Test/CoreTests/BindingsCrossInterfaceTest.cpp` 并在 `Test/CoreTests/SerializerTest.cpp` 注册，断言发行者与目标两类入口、目标接口真实调用、配对释放、错误类型、失效、无见证目标借用拒绝和过期见证释放清理。把两份夹具加入 `build.py`，保留 `WindowBindingsCompileTest.cpp` 现有发行者断言作为兼容对照。｜**验收**：未修生成器时构建或测试必须实际失败，且失败原因包含目标公开类型缺失、目标入口不存在或 `shared_ptr` 实参未重建之一，输出存为 `evidence/cross-interface-red.log`；不得只用方法名存在作为判据，也不得通过改写旧发行者断言来消除冲突。
- [x] 1.3 [US1][US2][US3] 新增 `Test/CoreTests/BindingsRenderFoundationTest.cpp` 并注册，使用固定非零字段验证原生收到的记录、枚举、掩码、名称、规范化路径、哈希对象所含值、字节和其他混合参数；新增 `Tools/CodeGen/BindableShapes.RenderReject*.h` 分别覆盖虚函数、非公开字段、非默认构造、缺少哈希来源、包装值返回/嵌入、`weak_ptr` 入参和未登记抽象接口，并在该测试中实现 `RunRenderRejectProbe` 逐个启动 `CodeGen.exe` 断言非零与诊断，空指针非零长度直接调用 thunk。扩展 `Scripting/Managed/SurfaceSignatureCheck.cs`、`BindingLayout.cs` 和 `Host.cs`，除委托签名外实际断言枚举常量与底层类型、`[Flags]`、记录字段顺序、`Marshal.SizeOf` / `Marshal.OffsetOf`，并加入目标释放与释放后调用探针；同步 `Tools/check_window_system_bindings.py` 的日志判据。｜**验收**：当前实现下原生行为断言、生成期拒绝探针或托管编译/启动检查实际变红，输出存为 `evidence/foundation-behavior-red.log`；每个负向夹具都有独立头文件、执行入口和预期诊断，不得只用生成器退出码 0、C++ 镜像或委托从不执行作为正向验收。

## 2. 实现统一转换与数据投影

- [x] 2.1 [US1][US3] 在 `Tools/CodeGen/CodeGenMain.cpp` 建立设计所述逐参数、逐返回值转换描述，让 C ABI、原生实参、返回转换、函数表槽、公开 C# 签名与调用共用同一来源；先迁移现有标量、文本、记录、智能指针和回调路径，不新增形状。｜**验收**：现有 `Tools/CodeGen/BindableShapes.expected.txt` 中非新增签名逐字不变，现有 CoreTests 绑定用例与 `Tools/CodeGen/check_bindings.py` 通过；临时删除任一已迁移参数的原生转换时，对应用例必须变红。保存 `evidence/conversion-plan-regression.log`，本条不要求 1.1 的新夹具转绿。
- [x] 2.2 [US1] 实现公开平坦/嵌套数据记录、记录字段中的真实底层枚举与 `EnumFlags.m_mask` 镜像，以及方法级 `EnumFlags` 按 `MaskType` 的定宽整数转换；补齐所有已接受定宽整数和 `bool` 的 C# 映射，禁止未知标量回退 `void*`。允许满足布局约束的静态工厂和非虚便利方法，保留标准布局、默认构造、公开字段、无虚函数及递归成员拒绝；同步真实 `sizeof` / `offsetof`、布局清单和 `Marshal.SizeOf` / `Marshal.OffsetOf` 自检。｜**验收**：`BindingsRenderFoundationTest` 的记录、方法级掩码和混合参数读数通过，`bool` 公开字段不是指针，`uint16_t` 掩码在函数表中是 `ushort`；CLR 实际大小和偏移与原生一致，负向夹具均非零并点名路径。把窄枚举统一写成 `int32_t`、漏掉方法级标志位或恢复“所有非默认方法都拒绝”时，对应测试必须变红，证据存入 `evidence/`。
- [x] 2.3 [US1] 从真实 C++ 声明生成公开 C# 枚举、平坦/嵌套记录和 `[Flags]` 标志位；按限定类型生成确定的共享 `.Bindings.cs`，在 `build.py` 对重复文件做哈希对账并拒绝同名不同限定名或同名不同内容。保持 `Scripting/Managed/Scripting.Managed.csproj` 的 `*.Bindings.cs` 收集方式，并由 `Host.Boot` 执行公开值与真实 CLR 布局检查。｜**验收**：1.3 的签名、枚举常量与底层类型、`[Flags]`、记录字段顺序、`Marshal.SizeOf` / `Marshal.OffsetOf` 断言全部通过；两个接口共用同一类型时托管程序集只有一个公开定义。分别改错一个枚举值、字段顺序或字段宽度，以及构造同名不同限定名或不同内容时，对应检查或构建必须实际失败并点名类型，红态存为 `evidence/shared-public-types-red.log` 后恢复。
- [x] 2.4 [US1] 实现独立 `NameHash` / `PathHash` 的 UTF-8 文本入参和独立 `HashObj<T>` 的所含值入参，原生侧分别调用现有文本构造函数与 `HashObj<T>{value}`；除记录条件外，生成器必须确认 `T` 具有消费翻译单元可见的生成哈希特化、`custom_hash_trait<T>` 或 `CA_HASH_BY_BYTES`。返回、嵌入记录或容器、缺少哈希来源及不满足条件的 `T` 必须在写产物前拒绝。｜**验收**：原生实现收到完整非 ASCII 名称、规范化路径和逐字段相同的 `Get()`，哈希状态与直接构造一致；缺少哈希来源的负向夹具在生成期非零并点名依赖，不能留到 C++ 静态断言。把生成器改为直接镜像包装值内部布局或取消哈希来源检查时测试必须变红，输出存入 `evidence/`。
- [x] 2.5 [US1] 在通用容器判定前实现 `castl::vector<uint8_t> const&` 的专用指针加数量投影和公开 `byte[]`，不得再拼出不存在的 `uint8_tC`；在托管调用层用 `fixed` 覆盖完整原生调用，在原生 thunk 中重建向量，通过统一转换描述支持字节与记录、标量、文本、枚举、标志位、句柄及智能指针返回的组合。｜**验收**：混合参数方法的所有原生读数正确；空数组合法、`null` 数组抛出参数异常、原生空指针配非零长度报错且实现调用次数为 0，调用返回后修改数组不改变原生已复制数据。只解除 `containerIn` 限制而未修标量字节路径、删除固定范围或让后续参数错位时测试必须变红，证据存入 `evidence/`。

## 3. 实现跨接口目标类型句柄

- [x] 3.1 [US2] 为 `CodeGen.exe` 增加 `--list-bind-interfaces`，从每个主头实际 `CA_BIND` 形状输出 `<短名>=<完整限定名>`；`build.py` 先聚合完整清单，再以重复的 `--known-interface <短名>=<完整限定名>` 传给正式生成。`CODEGEN_BINDINGS` 第三列继续只检查期望产物前缀，不再兼任接口清单；同短名不同限定名必须拒绝。｜**验收**：清单包含现有夹具全部实际标记接口，未列入期望前缀的 `IBindRow16` 等目标不会被误判；分头夹具双方按完整限定名组合，同短名冲突和移除抽象资源目标均非零并点名双方或缺失目标，输出存为 `evidence/known-interface-red.log` 后恢复。非接口数据对象仍只提供原生发行者句柄。
- [x] 3.2 [US2][US3] 句柄表项同时记录发行者与目标对象完整限定类型；现有 `ResolveHandle` / `ReleaseHandle` 和按方法释放保持发行者角色，函数表末尾追加 `ResolveTargetHandle` / `ReleaseTargetHandle` 供公开 C# 使用，ABI 版本不变。更新 `CSharpHost/private/CSharpHost.cpp` 的真实表大小断言。目标借用解析要求见证存在且存活，旧发行者无见证行为仅作兼容；两类释放都能在目标死亡或见证过期后清表项并进位。｜**验收**：`BindingsCrossInterfaceTest` 证明目标接口调到同一对象并能释放，错误目标、无见证和过期见证目标解析失败，过期释放仍清理；`WindowBindingsCompileTest.cpp` 的旧发行者错配、非接口数据强/弱句柄和按方法释放断言逐字保留并通过，现有回调不回归。把旧入口改成目标校验、让公开 C# 改用旧入口或允许任意类型通过时，对应测试必须变红，证据存入 `evidence/`。
- [x] 3.3 [US2] 实现 `shared_ptr<T>` 入参按目标类型反查与重建：强返回句柄复用强引用、弱返回句柄在调用期间取得强引用、借用句柄和错误类型拒绝；`weak_ptr<T>` 入参继续生成期拒绝。公开 C# 能从目标表面对象取得内部租约，目标释放后检查错误通道，`null` 和已释放对象不得进入原生实现；在 `Host.cs` 与 `Tools/check_window_system_bindings.py` 增加目标 `Dispose`、释放后 `ObjectDisposedException` 和原系统仍可用探针。｜**验收**：原生实现收到同一对象的 `shared_ptr<T>`，弱返回句柄调用期间有强保活，借用/错误类型/失效调用的实现计数为 0；把托管释放错接成发行者入口时 `Dispose` 实际报错，释放后调用被托管侧挡下，原窗口系统仍可调用。删除反查、支持 `weak_ptr` 入参或只做不执行的签名断言时对应测试必须变红，证据存入 `evidence/`。

## 4. 强化判据与同步文档

- [x] 4.1 [US1][US2][US3] 扩展 `Tools/CodeGen/check_bindings.py` 与 `Tools/CodeGen/BindableShapes.expected.txt`，逐条检查新增 C ABI、两侧方法集合、公开枚举/记录/包装值/字节签名、共享类型唯一性、完整限定名清单和真实 thunk 调用。`check_surface` / `check_alignment` 只处理有对应 `.Bindings.cpp` 的接口，共享公开值类型走独立判据；确认 `BindableShapes.Render*.h` 均被现有 `BindableShapes*.h` 规则识别为夹具而非真实接口。｜**验收**：分别临时构造缺失公开方法、共享文件被误当接口、重复或不同内容共享类型、未登记抽象接口、未转换实参和零值 thunk，每项都有非零输出并点名缺陷，保存 `evidence/check-bindings-mutations.log` 后全部恢复；现有回调、窗口和“夹具桩只打印、不参与判定”规则不得被削弱。
- [x] 4.2 [US1][US2] 更新 `Tools/CodeGen/README.md` 的投影矩阵、共享公开产物、`--list-bind-interfaces`、完整限定名 `--known-interface`、字节与包装值方向、发行者/目标两类句柄入口和当前限制；核对 `CSharpHost/README.md` 是否需补充追加槽后的表大小说明。给出可复制命令及真实输出，明确不支持 `std::span`、包装值返回/嵌入、`weak_ptr` 入参、异步字节和具体建图类。｜**验收**：`python Tools/CodeGen/verify_readme.py` 退出 0；文档中的命令路径、参数和表大小与生成物实际一致，已实现形状不得继续写成“留桩”或“本轮未实现”，非目标仍有明确拒绝说明。
- [x] 4.3 [US3] 更新 `docs/testing.md` 的 CoreTests 文件清单、运行读数和第三层检查清单，并核对 `Test/CoreTests/CMakeLists.txt` 的递归源码收集确实包含两个新测试文件。｜**验收**：文档中的 CoreTests 文件路径均存在，测试清单与 `Test/CoreTests/SerializerTest.cpp` 注册项一致，`CoreTests.exe` 输出中的 `PASSED` 行数与文档实测数字一致；若数字变化必须先取得真实输出再写文档，不得手算。

## 5. 全量回归与最终构建

- [x] 5.1 [US1][US2][US3] 在成功的增量构建后运行 `out/build/x64-debug/bin/CoreTests.exe`、`python Tools/CodeGen/check_bindings.py --product-dir out/build/x64-debug/codegen/bindings`、`python Tools/CodeGen/check_bindable_audit.py`，以及 `docs/testing.md` 既有命令形状的两个后端完整 `GPUBackendTester.exe --headless 10`；随后分别运行 `python Tools/check_window_system_bindings.py --log test_output/TestCSharpWindow.log` 和 `python Tools/check_window_system_bindings.py --log test_output/TestCSharpWindowCallbacks.log --callbacks`。｜**验收**：所有命令退出 0，新增测试、现有窗口/回调/弱句柄/借用句柄与两个后端全套测试均通过；报告和日志来自本次运行，窗口脚本参数与 `Tools/check_window_system_bindings.py` 的实际命令行一致。全部输出与退出码存入 `evidence/final-regression.log`，任何失败都必须修复后重跑，不能只保留成功样例。
- [x] 5.2 [US1][US2][US3] 最终运行 `python build.py --config Debug`，若失败则分析并修复直到输出 `BUILD SUCCESSFUL`；随后运行 `openspec validate csharp-render-binding-foundations`、`openspec validate --all`、`git diff --check -- . ':(exclude)openspec/changes/csharp-render-binding-foundations/evidence/**'` 和 `openspec instructions apply --change csharp-render-binding-foundations --json`。｜**验收**：构建成功，OpenSpec 校验通过，非证据文件没有空白错误，`remaining` 为 0 且本文件无未勾项；证据目录保留所有已声称的红态与恢复记录，工作区没有临时探针、崩溃转储或越出本 change 范围的实现改动。最终命令与输出存入 `evidence/final-build.log`。

## 6. 既有判据修订对照

以下修改依据是 `docs/ROADMAP.md` 阶段 4 对描述符、标志位、包装值和跨接口资源句柄的明确要求。所有旧场景仍保留；没有通过删除断言或放宽失败条件来取得通过。

### 6.1 `csharp-bindings`：接口产物与共享公开值类型

- **改前原文**：“生成器 MUST 为每个被标注为可绑定的接口产出两份文件：C++ 侧的 `<接口>.Bindings.cpp`（thunk + 函数表 + 取表函数 + 句柄表样板）与 C# 侧的 `<接口>.Bindings.cs`（包装类 + 句柄结构体 + 布局清单常量）。两份 MUST 来自同一次生成，方法集合与顺序 MUST 完全一致；MUST NOT 允许一侧手改而与另一侧错开。”
- **改后原文**：“生成器 MUST 为每个被标注为可绑定的接口产出两份接口文件：C++ 侧的 `<接口>.Bindings.cpp`（thunk + 函数表 + 取表函数 + 句柄表样板）与 C# 侧的 `<接口>.Bindings.cs`（包装类 + 句柄结构体 + 布局清单常量）。接口需要的公开记录和枚举可以另外产出按类型命名的共享 `.Bindings.cs`；这些文件 MUST 与引用它们的接口文件来自同一次生成。”另新增：“同一限定公开值类型的共享文件 MUST 具有确定内容。重复生成时内容必须逐字节相同，同名不同限定名或同名不同内容 MUST 生成失败；共享文件不得复制接口内部的 C 镜像、函数表或错误通道。”

### 6.2 `csharp-bindings`：发行者与目标入口及实际解析签名

- **改前原文一**：“`extern "C" void* <接口>_ResolveHandle(uint64_t handle)`”。
- **改后原文一**：“`extern "C" void* <接口>_ResolveHandle(void* self, uint64_t handle)`”。这只是使规范与当前生成物已有的 `self` 参数一致。
- **改前原文二**：“接口参数里的智能指针 MUST 反查句柄表，反查失败 MUST 只置错误码而不崩溃。”
- **改后原文二**：“`shared_ptr<T>` 入参 MUST 反查句柄表并重建目标类型，反查失败 MUST 只置错误码而不崩溃；`weak_ptr<T>` 入参 MUST 继续生成期拒绝。”另新增：“每个接口 MUST 在函数表末尾追加 `<接口>_ResolveTargetHandle` 与 `<接口>_ReleaseTargetHandle`，不得重排既有槽或改变 ABI 版本。公开 C# 的已绑定接口返回 MUST 使用目标入口，非接口数据对象的强/弱句柄继续使用发行者入口。”原有代际、弱句柄、强引用释放和原生发行者错配判据均未删除。

### 6.3 `csharp-bindings`：记录便利方法

- **改前原文**：“其他自定义方法 MUST 继续按非数据类型处理。”
- **改后原文**：“静态工厂、自定义比较和不改变数据布局的非虚便利方法也 MUST NOT 单独阻止镜像；记录仍须满足公开字段、标准布局、可默认构造和无虚函数。”
- **改前场景**：“换成一个自定义非默认方法时 MUST 拒绝并点名该类型。”
- **改后场景**：“前者 MUST 成功并保持字段布局，后者 MUST 非零退出并点名不满足条件的类型或成员；不得再把所有非默认方法统一视为拒绝理由。”其中前者是静态创建函数和非虚查询，后者是虚函数或非公开字段；对象语义拒绝条件没有放宽。

### 6.4 `csharp-bindable-surface`：shared_ptr 入参与双角色句柄

- **改前原文**：“接口接收智能指针参数的方法 MUST 投影为‘接收句柄’，由投影层反查。”
- **改后原文**：“接口接收 `shared_ptr<T>` 参数的方法 MUST 投影为‘接收句柄’，由投影层反查并重建目标类型的 `shared_ptr<T>`。`weak_ptr<T>` 入参不在本 change 的支持范围内，MUST 继续生成期拒绝；该排除不影响 `weak_ptr<T>` 返回形成弱句柄。”这里明确区分入参与返回，不把弱返回句柄的支持扩大成 `weak_ptr` 入参。
- **新增约束原文**：“句柄表项 MUST 同时记录发行者与目标对象的完整限定类型。”以及：“借用句柄没有可持有的智能指针所有权，作为 `shared_ptr<T>` 入参时 MUST 失败且不得进入实现。”新增目标接口入口不替代旧发行者入口，旧原生窗口断言继续作为验收。

### 6.5 `csharp-bindable-surface`：嵌套记录的纯数据条件

- **改前原文**：“结构体成员若属于另一个记录类型，投影层 MUST 只在那个类型自身及其全部祖先成员也都是标量或纯标量记录时递归镜像。”
- **改后原文**：“结构体成员若属于另一个记录类型，投影层 MUST 只在那个类型自身及其全部祖先成员也都是标量、枚举、标志位或纯数据记录时递归镜像。这里的纯数据记录 MUST 具有公开字段、标准布局、可默认构造且无虚函数；静态工厂、默认或自定义比较以及非虚便利方法不改变其纯数据身份。”
- **拒绝条件对照**：改前拒绝字符串、容器、指针、智能指针或自定义所有权成员；改后仍全部拒绝，并新增非公开字段、非默认构造或联合体的显式拒绝。新增允许项只涉及不改变数据布局的方法、枚举和标志位。
