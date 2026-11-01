## RENAMED Requirements

### Requirement: C ABI 面只允许三类形状

FROM: C ABI 面只允许三类形状
TO: C ABI 面只允许四类形状

## MODIFIED Requirements

### Requirement: C ABI 面只允许四类形状

引擎接口暴露给 C# 的那一层（下称“C ABI 面”）MUST 只使用四类形状：blittable 标量与递归纯标量记录、`T const* + 数量` 视图、不透明句柄（定宽整数 + 代际号）、以及“真实签名的原生函数指针 + 不透明 `void* user`”回调形状。回调注册号与注销伴生 MUST 留在内部层。容器类型（`castl::vector` / `map` / `unordered_map` / `set` / `string`）与模板类型 MUST NOT 出现在 C ABI 面上 —— 它们只在 C++ 接口面出现，由投影层翻译。

#### Scenario: 审计表里没有“直接把容器写在 C ABI 面”的行

- **WHEN** 复核 `evidence/bindable-audit.md` 里每一行的“投影形状”列
- **THEN** 该列只出现 blittable 类型、递归纯标量记录、`X const* + count`、`uint64_t 句柄`、`char const* + len`、以及“函数指针 + `void* user`”这些写法；出现一个容器类型名即算不合格

#### Scenario: 回调形状具有独立第四类而不是被硬塞进句柄

- **WHEN** 复核 10 个 `IWindowSystem::Set*Callback` 的投影行
- **THEN** 每行 MUST 明确写出原生函数指针、不透明用户令牌、内部注册号和注销伴生，回调首参 MUST 是回调期间借用的 `IWindow`，MUST NOT 再写成普通窗口资源句柄或禁止出现 `IWindow*`

#### Scenario: 模板只在投影层被单态化

- **WHEN** 某个接口方法的参数是 `castl::vector<BindingInfo>` 这样的具体化容器
- **THEN** 审计表 MUST 把它投影成“元素类型 + 数量”的形状（`BindingInfoC const* + count`），而接口本身保持 `castl::vector<BindingInfo>` 不变

### Requirement: 可投影但尚未实现的形状必须显式排除，不许静默发桩

审计表结论是“可绑”、但投影层还没有实现那条投影路径的形状，生成器 MUST NOT 静默生成“签名对、调用恒返回零”的桩。这类方法 MUST 由接口作者用 `CA_BIND_SKIP(方法, "理由")` 显式排除，理由里 MUST 出现“本轮未实现”并点名缺的是哪条投影路径；审计表里这些行的“可绑性”列 MUST 继续保持“可绑”，并在备注列写明“本轮未实现”。**这一类 MUST NOT 被写成 ①–④ 中的任何一类** —— 四类说的是“形状本身不可投影”，这一条说的是“实现进度”。

`castl::function<void(...)>` 回调与递归纯标量记录镜像已经由本 change 实现，MUST NOT 再把它们列为“本轮未实现”的例子。其余尚未实现的形状以 `Tools/CodeGen/README.md` 的当前清单为准；生成器 MUST 对“既没实现、又没被排除”的这类方法非零退出并点名。实现某一形状时必须同步删除对应接口排除、审计备注和文档备注，三者不得各自停留在不同阶段。

#### Scenario: 未实现的形状没被排除时生成器必须拒绝

- **WHEN** 在被标注接口里留一个仍属生成器未实现清单的形状，且不给它写 `CA_BIND_SKIP`
- **THEN** 生成器 MUST 非零退出，诊断里 MUST 点名该接口与方法，并 MUST 说明缺的是哪条投影路径；MUST NOT 生成一个空体 thunk

#### Scenario: 已实现的回调与嵌套镜像不得继续排除

- **WHEN** 复核 `IWindowSystem` 的 10 个回调、`GetMonitor`、唯一审计表和生成器文档
- **THEN** 这些条目 MUST NOT 再写“本轮未实现”；其他未实现形状的排除理由与行为 MUST 保持不变

## ADDED Requirements

### Requirement: 回调投影必须同时定义注册与保活生命周期

审计表中的每个可绑回调方法 MUST 写清原生函数指针、不透明用户令牌、内部注册号和注销伴生的投影形状，并明确替换、置空、表面对象释放、重入限制和异常上报方式。用户令牌 MUST 只作不透明数据，生成代码 MUST NOT 解引用它。公开 C# 表面 MUST 保持原 C++ 方法的 `void` 返回语义，内部注册号不得成为公开返回值。

#### Scenario: 回调审计行覆盖完整生命周期

- **WHEN** 复核 `IWindowSystem` 的 10 个回调审计行
- **THEN** 每行 MUST 能对应生成器实际产出的注册和注销形状，并且替换、置空、释放与异常行为都有一致的规范依据

### Requirement: 嵌套记录只允许递归纯标量投影

结构体成员若属于另一个记录类型，投影层 MUST 只在那个类型自身及其全部祖先成员也都是标量或纯标量记录时递归镜像。布局清单 MUST 保留嵌套字段路径和真实偏移；任何一层出现字符串、容器、指针、智能指针或自定义所有权成员时，MUST NOT 将该结构体视为 blittable。

#### Scenario: MonitorInfo 的两个矩形与 DPI 都有独立镜像字段

- **WHEN** 投影 `MonitorInfo`
- **THEN** 两个 `FloatRect` 与 `m_DPIScale` MUST 分别出现在同源布局清单和公开 C# 记录类型中，MUST NOT 用匿名字节数组代替字段

#### Scenario: 嵌套层级里藏有非 blittable 成员时必须拒绝

- **WHEN** 一个外层标量记录包含另一个记录，而后者含有字符串、容器、指针、智能指针或自定义所有权成员
- **THEN** 生成器 MUST 非零退出并点名嵌套成员路径
