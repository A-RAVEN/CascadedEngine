## ADDED Requirements

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
