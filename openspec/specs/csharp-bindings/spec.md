# csharp-bindings Specification

## Purpose
按冻结的投影矩阵把被标注的 C++ 接口生成成两侧绑定（C ABI thunk + 版本化函数表 + 句柄表 + 布局清单 + C# 声明），并规定生成期怎么拒绝无法投影的形状。
## Requirements
### Requirement: 一个接口一份产物，两侧由同一次生成产出
生成器 MUST 为每个被标注为可绑定的接口产出两份文件：C++ 侧的 `<接口>.Bindings.cpp`（thunk + 函数表 + 取表函数 + 句柄表样板）与 C# 侧的 `<接口>.Bindings.cs`（包装类 + 句柄结构体 + 布局清单常量）。两份 MUST 来自同一次生成，方法集合与顺序 MUST 完全一致；MUST NOT 允许一侧手改而与另一侧错开。

#### Scenario: 两侧方法集合逐条对齐
- **WHEN** 对一个被标注接口跑生成器
- **THEN** 生成的 C++ thunk 数量、名字顺序 MUST 与 C# 包装里的方法数量、名字顺序逐条相同（比对脚本给出条数与逐条差异）

### Requirement: 生成的 C ABI 面只允许审计表规定的形状
生成器 MUST 只按 `csharp-bindable-surface` 的投影矩阵出形状：blittable 标量与结构体、`T const* + 数量`、不透明句柄、`char const* + len`、以及**函数指针 + `void* user`**（回调那一行；`user` 只作不透明令牌，生成代码 MUST NOT 解引用它）。生成的 C ABI 签名里 MUST NOT 出现容器类型（`castl::vector` / `map` / `string` 等）、模板、`shared_ptr` / `unique_ptr` / `weak_ptr`、C++ 引用。

#### Scenario: 签名里出现容器类型即算不合格
- **WHEN** 扫描生成的 `<接口>.Bindings.cpp` 里所有 `extern "C"` 声明
- **THEN** 容器类型名、模板尖括号、`shared_ptr`/`unique_ptr`/`weak_ptr`、C++ `&` 引用的命中数 MUST 全为 0（脚本逐类给命中数与命中行）

### Requirement: 回调必须带保活与注销，异常必须转成错误码
矩阵里"回调"那一行本轮**不生成**：带 `castl::function` 参数的方法 MUST 由接口作者用 `CA_BIND_SKIP(方法, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")` 显式排除，MUST NOT 生成"签名对、调用即空转"的桩；这类排除的口径与 `csharp-bindable-surface` 里"可投影但尚未实现的形状"一致 —— 审计表里它们的结论仍是"可绑"，本 change 不改这个结论，只在备注列写"本轮未实现"。矩阵里"会抛异常的方法"这一半保持不变：MUST 在生成的 thunk 里 try/catch，并 MUST 转成"错误码 + 一条文本"返回；MUST NOT 让异常穿过 C ABI。

#### Scenario: 未排除的回调方法必须让生成器拒绝
- **WHEN** 在被标注接口里留一个带 `castl::function` 参数的方法，且不给它写 `CA_BIND_SKIP`
- **THEN** 生成器 MUST 非零退出并点名该接口与方法，MUST NOT 生成空体 thunk

#### Scenario: 抛异常的方法只回错误码与文本
- **WHEN** 调用一个在实现里抛异常的已绑定方法
- **THEN** 调用方 MUST 收到非零错误码与一条非空文本，进程 MUST NOT 崩溃、异常 MUST NOT 穿出 thunk

### Requirement: 函数表版本化，且本 change 只做"版本相等"校验
生成的函数表 MUST 是带 `abi_version` 与 `size` 的结构体，由 `extern "C"` 的取表函数填充并返回实际大小；取表函数的签名 MUST 让调用方把自己的版本与大小传进来（`GetApi(uint32 version, uint32 size, <表>* out)`），引擎侧 MUST 只写调用方 `size` 与真实表大小中较小的那一段，并 MUST 回填自己真实的大小。**本 change 只要求"版本相等 + 大小相等"这一档**（与已落地的 `Scripting/Managed/Host.cs` 校验口径一致）；"旧侧按自己较小的 size 也能通过"的追加兼容 MUST NOT 写进本 change 的验收 —— 在 `GetApi(out)` 这种只有出参的签名下它不成立（引擎不知道调用方缓冲区多大，写下去就越界）。

#### Scenario: 版本或大小不等必须拒绝
- **WHEN** 用错误的 `abi_version` 或错误的 `size` 调取表函数
- **THEN** MUST 返回 0（表示拒绝）且 MUST NOT 往调用方缓冲区里写超出其 `size` 的字节

#### Scenario: 相等时通过并回填真实大小
- **WHEN** 用正确的版本与大小调取表函数
- **THEN** MUST 返回真实表大小，且调用方缓冲区里的字段与生成物逐项一致

### Requirement: 句柄分配与释放由生成物负责，且带代际号
生成的句柄 MUST 同时编码序号与代际号；释放 MUST 让代际号进位，使旧句柄 MUST 被识别为失效而不是命中新对象。释放 MUST 让生成物不再持有该对象的强引用 —— 释放之后，句柄表里那一项的强引用 MUST 为空，否则对象永不被析构。**每个被绑定的接口 MUST 无条件生成两个泛用入口**：`extern "C" void <接口>_ReleaseHandle(void* self, uint64_t handle)` 与 `extern "C" void* <接口>_ResolveHandle(uint64_t handle)`（后者把句柄换成对象指针，失败时置"句柄失效"错误码并返回空）。**`ResolveHandle` 是必需的**：C ABI 面所有方法入口的第一个参数都是 `void* self`，没有任何一个收定宽句柄，没有解析入口就无法用句柄调到方法。既有的按方法生成的 `<接口>_Release<方法名>` MUST 保留（不得为了改名去动既有调用点）。返回 `castl::weak_ptr<T>` 的方法 MUST 投影成**弱句柄**：句柄表存弱引用、MUST NOT 抬高 `use_count()`，对象析构后旧句柄 MUST 被判失效 —— 且这个失效判定 MUST 走"弱引用已过期"这条路径，MUST NOT 只靠释放时的代际进位来产生。接口参数里的智能指针 MUST 反查句柄表，反查失败 MUST 只置错误码而不崩溃。MUST NOT 生成"句柄背后的内存由托管侧 free/delete"这种跨边界所有权。

#### Scenario: 释放后的旧句柄必须报失效
- **WHEN** 创建一个资源拿到句柄、释放它、再用同一个数值调用任一方法
- **THEN** 调用 MUST 返回"句柄失效"错误码（不是崩溃、也不是命中新对象），且错误码可与其他错误区分

#### Scenario: 释放后生成物不再持有强引用
- **WHEN** 在夹具上创建一个资源拿到句柄，释放该句柄，然后读句柄表里那一项的强引用字段（产物被 `#include` 进测试，那张表在同一个翻译单元里可见）
- **THEN** 该强引用 MUST 为空；若不为空，这条判据 MUST 变红。判据 MUST NOT 用 `use_count()` 差值代替（差值既会被"删得不干净"骗过，也会把外部合法持有者判红）

#### Scenario: 弱句柄在对象被回收后判失效，且不靠释放动作
- **WHEN** 用句柄的解析入口把弱句柄换成对象指针、调到方法之后，让持有方自己回收对象（整个过程中 MUST NOT 调任何释放入口），再用同一个句柄调解析入口
- **THEN** 解析入口 MUST 返回空且错误码为"句柄失效"；同时用一个 `std::weak_ptr` 见证者的 `expired()` MUST 为真，证明对象确实已经析构。MUST NOT 用"读那个忽略 `self` 的每接口全局错误码"来证明这件事

#### Scenario: 弱句柄不改变持有方与引用计数
- **WHEN** 夹具方法返回 `castl::weak_ptr<T>` 拿到句柄，比较拿到句柄前后的 `use_count()`
- **THEN** 两次读数 MUST 相同

### Requirement: 布局清单与启动自检
生成器 MUST 为每个过界结构体同时产出 C++ 与 C# 两份同源常量（`size` + 每个字段 `offset`）。托管侧启动时 MUST 逐项比对本侧清单与引擎侧清单，不一致 MUST 点名"哪个类型哪个字段"并拒绝继续初始化。

#### Scenario: 故意改一个字段宽度后启动必须报错
- **WHEN** 临时把某个过界结构体里一个字段从 `uint32_t` 改成 `uint64_t` 而不重新生成
- **THEN** 托管侧启动自检失败，错误信息 MUST 出现该类型名与字段名，且初始化 MUST 被拒绝

### Requirement: 不可投影的形状必须在生成期点名，显式排除必须写理由
本能力沿用 `csharp-bindable-surface` 的四类不可投影（① 模板方法或泛型接口；② 所有权不明的裸指针；③ 依赖隐式转换的重载；④ 语义越界），MUST NOT 改动这四类的定义。生成器对 ① 与 ③ MUST 自动识别并在生成期非零退出、逐条打印"接口::方法 + 类别 + 建议"；对 ② 与 ④ MUST 要求接口作者用 `CA_BIND_SKIP(方法, "理由")` 逐方法排除（因为同样形状的方法在审计表里有的可绑、有的不可绑，机器判不出来）。带理由的排除 MUST 只打印、退出码 0；**排除但没写理由**是独立的一条错误，MUST 非零退出并点名该方法。生成器 MUST NOT 对不可投影的方法生成"能编译但调用即崩"的绑定。

#### Scenario: 未写理由的排除必须报错
- **WHEN** 在接口里排除一个方法但理由留空
- **THEN** 生成器非零退出，诊断里 MUST 点名该方法并说明"排除必须写理由"

#### Scenario: 带理由的语义越界排除只打印不报错
- **WHEN** 排除 `CRenderBackend::Release`（或 `IOManager::Initialize`）并写明理由
- **THEN** 生成器退出码为 0，日志里 MUST 出现该方法的排除理由，且产物里 MUST 有它被排除的注释

#### Scenario: 模板方法与隐式转换重载自动报错
- **WHEN** 在被标注接口里加一个 `template<typename T> void Set(T const&)` 方法
- **THEN** 生成器非零退出，诊断里 MUST 把它归到 ① 类并给建议；不需要任何 `CA_BIND_SKIP`

### Requirement: 未标注的接口一律不处理
生成器 MUST 只处理被显式标注为可绑定的接口；未被标注的接口 MUST 既不生成产物也不报错，且 MUST 在日志里列出生成了哪些接口、跳过了哪些接口（跳过清单可核）。

#### Scenario: 未标注接口不产出任何文件
- **WHEN** 对一个没有被标注的接口所在模块跑生成器
- **THEN** 该接口的产物文件数 MUST 为 0，且日志的"跳过"清单里 MUST 列出它

### Requirement: 每个矩阵行必须有 conformance 夹具
生成器 MUST 让投影矩阵的每一行都能指到一个夹具接口（矩阵在 `Tools/CodeGen/README.md` 第十三节，行数与该节的表逐字一致 —— 本 change 之前是 14 行、之后是 15 行），并 MUST 有脚本按行打印"矩阵行 → 夹具"映射、断言每一行恰好对应一个夹具、且该夹具生成的 C ABI 签名逐字符合矩阵里写的形状。矩阵里 `castl::weak_ptr<T>` 那一行**不再是"不投影"行**：它 MUST 改成弱句柄正向夹具，断言生成出 `uint64_t` 签名。四类不可投影的夹具仍然各自独立存在（拒绝理由、理由为空、模板、重载），MUST NOT 因为矩阵里不再有"不投影"行就把它们删掉。"会抛异常"那一行 MUST 断言"错误码 + 文本"这种输出，而不是比对签名。MUST NOT 用"矩阵行数与夹具数相等"作为判据 —— 那在缺行、多出重复夹具时会同时成立而假绿。

#### Scenario: 矩阵每一行都能指到一个夹具
- **WHEN** 跑 conformance 脚本
- **THEN** 输出的映射行数 MUST 等于矩阵实际行数（本 change 之后是 15），每行恰好一个夹具；"无夹具的行数"与"签名不符数" MUST 都为 0

#### Scenario: weak_ptr 那一行必须生成弱句柄而不是被拒绝
- **WHEN** 跑 conformance 脚本里 `weak_ptr` 那一行
- **THEN** 它 MUST 是一条正向签名断言（返回 `uint64_t`），MUST NOT 再走"生成器非零退出"的拒绝路径；`expected.txt` 里那一行 MUST 是签名而不是"拒绝：…"字样

#### Scenario: 缺一行的夹具时报错并点名
- **WHEN** 临时删掉"非 blittable 元素容器"那一行的夹具
- **THEN** conformance 脚本 MUST 非零退出，并 MUST 点名缺的是哪一行（按矩阵行号 + 行文），而不是只打一个总数

### Requirement: 生成器先核对审计表
生成器在生成前 MUST 核对"被标注接口的每个纯虚方法在审计表里都有结论"，对不上 MUST 非零退出并点名缺哪条。MUST NOT 让审计表与生成器各自演进而互不知情。

#### Scenario: 接口新增方法而审计表没跟上时生成器必须拒绝
- **WHEN** 给被标注接口加一个纯虚方法但不更新审计表
- **THEN** 生成器非零退出，诊断里 MUST 点名"哪个接口的哪个方法缺审计结论"

### Requirement: 托管侧的绑定产物必须参与编译
生成的 `<接口>.Bindings.cs` MUST 被编进托管程序集；构建 MUST 把生成物所在目录以属性传给托管工程。托管侧 MUST NOT 手抄生成物里的常量（函数表大小、函数表成员偏移），MUST 直接引用生成物里的常量。

#### Scenario: 生成物里的类型能从程序集里找到
- **WHEN** 构建完成后在托管程序集里查生成物声明的类型名与常量名
- **THEN** 它们 MUST 都存在；MUST NOT 只在磁盘上有一份没人编译的 `.cs`

#### Scenario: 生成物缺失时构建必须失败
- **WHEN** 不传生成物目录属性（或目录里没有 `.cs`）就编托管工程
- **THEN** 构建 MUST 失败并说明缺的是生成物目录，MUST NOT 静默编出一个缺绑定的程序集

### Requirement: 被绑定到托管侧的接口，其函数表里不许有桩
**对真实接口**，每个进入函数表的槽 MUST 真的调用了实现。判定口径 MUST 是结构的、且 MUST 能挡住"看起来调了"的写法：先剥掉 `//` 与 `/* */` 注释，再要求函数体里出现 `object-><方法名>(` 形式的调用，且该调用的结果 MUST 出现在 `return` 里或被赋给命名变量（投影返回 `void` 的槽允许只调用）；`decltype` / `sizeof` / `noexcept` 上下文里的出现、以及 `(void)` 前缀丢弃结果的出现 MUST NOT 算作调用了实现。**另有一条独立红条件**：投影后的返回类型是 `void`、而实现返回的原始拼写去掉 `const` 后不是 `void` 时 MUST 判为缺陷。MUST NOT 用"某句注释文本出现了几次"当判据。对夹具接口，桩数 MUST 被打印出来但 MUST NOT 参与判定。同时 MUST 断言"该接口的 thunk 数等于独立清单里的方法数"，否则整个方法被排除掉也能全绿。

#### Scenario: 真实接口的桩数必须为 0
- **WHEN** 跑绑定判据脚本，解析真实接口产物里函数表成员对应的 thunk 函数体
- **THEN** 桩数 MUST 为 0；非 0 时脚本 MUST 非零退出并逐条打印"接口 + 方法 + 该槽体内既没有调实现"；修复前必须能读出 10 个

#### Scenario: 注释与丢弃结果的写法都不许喂绿
- **WHEN** 把某个桩体的那句形状注释改写成含 `object->` 的文本，或把 thunk 写成 `(void)object->M(); return {};`
- **THEN** 判据 MUST 仍然把该槽判为桩并非零退出

#### Scenario: 调了实现但丢掉返回值也算缺陷
- **WHEN** 某个方法的实现返回非 `void`（例如 `void*`），而生成的 thunk 只写了 `object->M();`、投影后的返回类型却是 `void`
- **THEN** 判据 MUST 把该槽判为缺陷并非零退出

#### Scenario: 整个方法被静默排除时必须被发现
- **WHEN** 把某个方法从发射循环里过滤掉，使它的 thunk 与函数表成员一起消失
- **THEN** 判据 MUST 非零退出并点名"该接口少了哪个方法"；MUST NOT 因为桩数为 0、两侧对齐 0 处就通过

#### Scenario: 算不出真实接口时必须报判据失效
- **WHEN** 把一个只有夹具产物的目录传给判据
- **THEN** 判据 MUST 退出 2 并打印"算不出真实接口"，MUST NOT 因为"真实接口集合为空"就当作 0 桩通过

### Requirement: 构建必须跑绑定判据
`python build.py` MUST 调用 `Tools/CodeGen/check_bindings.py`，MUST 把当前构建配置的产物目录传给它，并 MUST 转发该脚本的 stdout（否则判据打印的夹具桩数会被吞掉），该脚本非零退出时构建 MUST 失败。判据脚本的产物目录 MUST 只有一个来源（命令行参数），MUST NOT 在脚本里另留一份硬编码路径给签名对拍或两侧对齐用。MUST NOT 让签名对拍与桩计数只作为一条需要人手敲的命令存在。

#### Scenario: 把某个方法的实现改成桩后构建必须失败
- **WHEN** 临时把发射器源码改成让 `IWindow` 某个槽落回只返回零值的分支，再跑 `python build.py --config Debug`
- **THEN** 构建 MUST 失败，输出里 MUST 点名该接口与该桩，MUST NOT 出现 `BUILD SUCCESSFUL`；还原后构建恢复通过。造这条红态 MUST NOT 靠修改生成物（构建会重新生成覆盖掉），也 MUST NOT 靠给该方法加排除理由（那样 thunk 直接消失、桩数不升）

#### Scenario: 构建日志里必须看得到夹具桩数
- **WHEN** 跑一次成功的 `python build.py --config Debug`
- **THEN** 构建日志里 MUST 出现一行夹具桩数（本轮修完应为 6），MUST NOT 被 `capture_output` 吞掉而只剩一行产物摘要

#### Scenario: 判的必须是当前配置的产物
- **WHEN** 不带 `--config` 跑 `python build.py`（默认配置的产物在另一个目录）
- **THEN** 判据 MUST 读该配置目录下的产物；`build.py` MUST 把它传给判据的那个目录逐字打印出来，该行 MUST 是当前预设的目录（不带 `--config` 时应出现 `x64-relWithDebugInfo`）

#### Scenario: 产物目录不存在必须与发现桩分开
- **WHEN** 直接跑判据脚本、把产物目录指向一个不存在的目录
- **THEN** 脚本 MUST 退出 2 并打印"产物目录不存在，判据自身失效"，MUST NOT 与"发现桩"共用退出码 1。MUST NOT 用"把构建目录改名再跑 build.py"造这条红态 —— 构建自己会重建那个目录

### Requirement: 标量、枚举与不透明指针的返回值必须走真实现
返回 `bool` 的方法 MUST 投影成 `int32_t` 并真实回读实现的结果（`0` / `1`）；返回枚举的方法 MUST 投影成 `int32_t` 并用显式转换回读（枚举不能隐式转整数），且 MUST 有一条经 C ABI 面的枚举值读数断言；返回 `void*` 的方法 MUST 投影成 `uint64_t` 的不透明令牌并真实回读（MUST NOT 被当成返回 `void` 而丢弃）。**`uint64_t` 这条投影 MUST 只适用于 `void*` 这一种拼写** —— 其它 `T*` 返回 MUST 仍走"拒绝 + 要求 `CA_BIND_SKIP` 写理由"，MUST NOT 被静默绑成裸地址令牌。

#### Scenario: bool 返回值在两侧都能取到真值
- **WHEN** 让一个返回 `bool` 的方法分别返回真与假，各从 C ABI 面读一次
- **THEN** 两次读数 MUST 分别是 `1` 与 `0`；修复前这个槽恒为 `0`，所以这条判据必须能变红

#### Scenario: 枚举返回值不许编不过
- **WHEN** 对一个返回 `enum class` 的方法跑生成器并编译
- **THEN** 生成的 thunk MUST 用显式转换把枚举转成 `int32_t`，构建 MUST 通过；写成直接返回枚举 MUST 编不过（这条判据靠构建失败来证明它有效）

#### Scenario: 返回 void* 的方法不能丢掉返回值
- **WHEN** 让一个返回 `void*` 的方法返回一个非零地址，从 C ABI 面读回
- **THEN** 读回的值 MUST 等于那个地址；MUST NOT 返回 0、也 MUST NOT 把该槽发成返回 `void`。断言 MUST 用一个假实现（返回固定哨兵地址）来做，MUST NOT 要求真窗口 —— 这一层的测试不加载窗口模块、开不了窗口

#### Scenario: 其它指针返回不许走不透明令牌
- **WHEN** 给被标注接口加一个返回 `T*`（不是 `void*`）且没写 `CA_BIND_SKIP` 的方法
- **THEN** 生成器 MUST 非零退出并点名该方法，MUST NOT 把它投影成 `uint64_t` 裸令牌

### Requirement: 字符串入参与两段式返回必须真有实现
字符串入参 MUST 由 `char const* + len` 构造出实现要的类型后再调用。返回字符串（`castl::string_view` 或按值的 `castl::string`）的方法 MUST 生成"先问长度、再拷出"的两个伴生函数，且**每个伴生自己实现**：取长度那次与拷出那次各自重新取一次实现的值，MUST NOT 依赖跨调用的暂存状态。拷出那次 MUST 按调用方给的容量截断并以零结尾，**容量为 0 时 MUST 一个字节都不写**（容量是无符号的，"容量减一"在 0 时会下溢成最大值）；长度由取长度那个伴生提供，拷出那次 MUST NOT 承诺回填长度。**这类方法 MUST NOT 进函数表，也 MUST NOT 发 `extern "C"` 直出入口与直出 thunk** —— 只删表槽会把空体 thunk 留在产物里（被桩判据算成一个桩），整段排除又会把刚发的两个伴生一起删掉，三个都不发才自洽。同时 MUST 断言"取长度那次给的长度 == 拷出那次写入的字节数"，否则长度算错、拷贝只写一半的实现能同时骗过两个单独断言。

#### Scenario: 两个伴生都必须有函数体
- **WHEN** 在生成物里查取长度与拷出这两个伴生符号
- **THEN** 每个符号 MUST 至少有两处命中（一处声明、一处定义）；只有声明的不算通过；同时 MUST NOT 存在对应的 `extern "C"` 直出入口

#### Scenario: 长度与拷出必须对得上
- **WHEN** 经 C ABI 面设一个非空名字，先调取长度、再调拷出
- **THEN** 取长度给出的值 MUST 等于设进去的名字长度，也 MUST 等于拷出写入的字节数；三者不等即算不合格（只断言"取长度非 0"会被长度算错的实现骗过）

#### Scenario: 截断时仍必须以零结尾
- **WHEN** 用一个明显小于文本长度的容量调拷出伴生
- **THEN** 写入的字节数 MUST 小于容量、最后一个字节 MUST 是零；且不崩

#### Scenario: 容量为 0 时一个字节都不写
- **WHEN** 用容量 0 调拷出伴生
- **THEN** MUST NOT 写入任何字节、MUST NOT 崩（这条挡的是"容量减一"在无符号下溢后写满整段的情况）

