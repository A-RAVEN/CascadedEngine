## MODIFIED Requirements

### Requirement: 回调必须带保活与注销，异常必须转成错误码
矩阵里"回调"那一行本轮**不生成**：带 `castl::function` 参数的方法 MUST 由接口作者用 `CA_BIND_SKIP(方法, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")` 显式排除，MUST NOT 生成"签名对、调用即空转"的桩；这类排除的口径与 `csharp-bindable-surface` 里"可投影但尚未实现的形状"一致 —— 审计表里它们的结论仍是"可绑"，本 change 不改这个结论，只在备注列写"本轮未实现"。矩阵里"会抛异常的方法"这一半保持不变：MUST 在生成的 thunk 里 try/catch，并 MUST 转成"错误码 + 一条文本"返回；MUST NOT 让异常穿过 C ABI。

#### Scenario: 未排除的回调方法必须让生成器拒绝
- **WHEN** 在被标注接口里留一个带 `castl::function` 参数的方法，且不给它写 `CA_BIND_SKIP`
- **THEN** 生成器 MUST 非零退出并点名该接口与方法，MUST NOT 生成空体 thunk

#### Scenario: 抛异常的方法只回错误码与文本
- **WHEN** 调用一个在实现里抛异常的已绑定方法
- **THEN** 调用方 MUST 收到非零错误码与一条非空文本，进程 MUST NOT 崩溃、异常 MUST NOT 穿出 thunk

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

## ADDED Requirements

### Requirement: 托管侧的绑定产物必须参与编译
生成的 `<接口>.Bindings.cs` MUST 被编进托管程序集；构建 MUST 把生成物所在目录以属性传给托管工程。托管侧 MUST NOT 手抄生成物里的常量（函数表大小、函数表成员偏移），MUST 直接引用生成物里的常量。

#### Scenario: 生成物里的类型能从程序集里找到
- **WHEN** 构建完成后在托管程序集里查生成物声明的类型名与常量名
- **THEN** 它们 MUST 都存在；MUST NOT 只在磁盘上有一份没人编译的 `.cs`

#### Scenario: 生成物缺失时构建必须失败
- **WHEN** 不传生成物目录属性（或目录里没有 `.cs`）就编托管工程
- **THEN** 构建 MUST 失败并说明缺的是生成物目录，MUST NOT 静默编出一个缺绑定的程序集
