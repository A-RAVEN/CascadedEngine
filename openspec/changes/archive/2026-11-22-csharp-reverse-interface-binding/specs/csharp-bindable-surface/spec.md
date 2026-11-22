# csharp-bindable-surface 增量

## RENAMED Requirements

- FROM: `### Requirement: C ABI 面只允许四类形状`
- TO: `### Requirement: C ABI 面只允许五类形状`
- FROM: `### Requirement: 回调投影必须同时定义注册与保活生命周期`
- TO: `### Requirement: 回调投影必须定义完整生命周期`

## MODIFIED Requirements

### Requirement: C ABI 面只允许五类形状
引擎接口暴露给 C# 的那一层（下称"C ABI 面"）MUST 只使用五类形状：blittable 标量与递归纯标量记录、`T const* + 数量` 视图、不透明句柄（定宽整数 + 代际号）、"真实签名的原生函数指针 + 不透明 `void* user`"回调形状、以及**反向接口的"一张函数表 + 一个不透明 `void* user`"形状**（独立第五类，第 4 类仍是单回调；函数表含接口每**被绑定**虚方法投影出的函数指针，**一个虚方法 MAY 投影成多条**如两段式 Len/Copy；每条函数指针的参数 MUST 仍落在前四类内）。回调注册号与注销伴生 MUST 留在内部层。容器类型（`castl::vector` / `map` / `unordered_map` / `set` / `string`）与模板类型 MUST NOT 出现在 C ABI 面上 —— 它们只在 C++ 接口面出现，由投影层翻译。

#### Scenario: 审计表里没有"直接把容器写在 C ABI 面"的行

- **WHEN** 复核 `evidence/bindable-audit.md` 里每一行的"投影形状"列
- **THEN** 该列只出现 blittable 类型、递归纯标量记录、`X const* + count`、`uint64_t 句柄`、`char const* + len`、"函数指针 + `void* user`"、以及"函数表 + `void* user`"这些写法；出现一个容器类型名即算不合格

#### Scenario: 回调形状具有独立第四类而不是被硬塞进句柄

- **WHEN** 复核 10 个 `IWindowSystem::Set*Callback` 的投影行
- **THEN** 每行 MUST 明确写出原生函数指针、不透明用户令牌、内部注册号和注销伴生，回调首参 MUST 是回调期间借用的 `IWindow`，MUST NOT 再写成普通窗口资源句柄或禁止出现 `IWindow*`

#### Scenario: 模板只在投影层被单态化

- **WHEN** 某个接口方法的参数是 `castl::vector<BindingInfo>` 这样的具体化容器
- **THEN** 审计表 MUST 把它投影成"元素类型 + 数量"的形状（`BindingInfoC const* + count`），而接口本身保持 `castl::vector<BindingInfo>` 不变

#### Scenario: 反向接口形状是函数表加 user 且参数仍在前四类

- **WHEN** 复核反向接口（C# 实现 C++）的投影行
- **THEN** 该行 MUST 写成"函数表（每虚方法投影出的函数指针，可多条）+ `void* user`"，每条函数指针的参数 MUST 只含 blittable/视图/句柄/内存窗；出现容器/模板/智能指针过界即算不合格

### Requirement: 回调投影必须定义完整生命周期
每个 `castl::function<void(...)>` 回调 MUST 在审计表中明确登记为"函数指针 + 不透明用户令牌 + 内部注册号/注销伴生"的投影，**MUST NOT 被排除**。投影规格 MUST 定义注册、替换、置空、注销、表面对象释放、重入和异常传播的完整语义：回调参数 MUST 保持原顺序；用户令牌 MUST 只作不透明数据，**原生生成代码（C++ thunk/代理）MUST NOT 解引用它**（原生侧只把 user 原样传给函数指针）；**托管桥（`[UnmanagedCallersOnly]` 静态方法）用 `GCHandle.FromIntPtr(user).Target` 解回托管对象是第 4 类形状的既有语义**（现有 `OnSetKeyCallback` 已如此），反向接口的 C# 实例解回同此语义，不构成对"原生不得解引用"的违反。公开 C# MUST 只暴露托管委托或表面对象，MUST NOT 暴露注册号、函数指针或用户令牌；注册表 MUST 保活委托，替换/置空/释放 MUST 同时清理原生注册与托管保活项。

回调首参若是已绑定接口的裸指针，审计表 MUST 把它投影成"回调期间借用的表面对象"，而不是可长期持有的资源句柄。借用对象只在回调执行期间有效；回调返回后 MUST 撤销借用状态，后续调用 MUST 抛出明确失效异常。回调执行期间修改同一个注册 MUST 失败且不得销毁正在执行的回调；用户委托异常 MUST 在托管桥内转成错误状态，MUST NOT 穿过原生回调边界，下一次主线程调用 MUST 通过对应接口异常类型上报统一错误表中的回调错误标识和原始文本。

回调投影 MUST 有正向 conformance 夹具，至少执行注册、调用、替换、置空、表面对象释放、回调后借用失效、执行期修改注册失败和用户委托异常转错误码。生成器 MUST NOT 为未排除的回调方法生成空体 thunk；`expected.txt` 与 README MUST NOT 再把这类方法登记为本轮拒绝项。

#### Scenario: 未排除的回调方法生成真实注册与调用

- **WHEN** 对一个带 `castl::function<void(...)>` 参数的已审计方法生成绑定，并从托管侧注册委托后触发原生回调
- **THEN** 生成器 MUST 成功，托管委托 MUST 按原参数类型与顺序收到真实读数，且原生实现确实持有由生成代码安装的回调

#### Scenario: 回调参数顺序不得改变

- **WHEN** 夹具回调包含窗口、两个整数、两个浮点数和窄枚举参数
- **THEN** 托管委托收到的参数顺序 MUST 与 C++ `castl::function<void(...)>` 完全一致，不能把用户令牌插到公开委托参数中

#### Scenario: 替换和置空必须清理旧注册

- **WHEN** 先注册一个委托，再用另一个委托替换，随后置空并分别触发原生事件
- **THEN** 替换后只有新委托执行，置空后两个旧委托都不再执行，生成注册表中对应保活项 MUST 已删除

#### Scenario: 表面对象释放必须注销回调

- **WHEN** 注册回调后释放持有它的 C# 表面对象，再触发原生事件
- **THEN** 已释放对象的委托 MUST NOT 再执行，原生注册与托管保活项 MUST 已清理，其他表面对象的回调 MUST 仍可用

#### Scenario: 回调借用对象返回后必须失效

- **WHEN** 用户委托保存回调收到的接口对象，并在回调返回后调用它的方法
- **THEN** 调用 MUST 抛出明确失效异常，进程 MUST 不崩溃；回调执行期间的同名调用 MUST 能读到真实对象数据

#### Scenario: 回调执行期间修改同一个注册必须失败

- **WHEN** 用户委托在执行期间尝试替换或清空它自己
- **THEN** 桥 MUST 返回可诊断错误且不得销毁正在执行的回调，进程 MUST 继续运行

#### Scenario: 用户委托抛异常不得跨边界

- **WHEN** 一个已注册委托抛出异常，随后主线程再调用 `UpdateSystem` 或回调注册方法
- **THEN** 异常 MUST NOT 穿出原生回调边界，后续公开调用 MUST 抛出对应接口异常并包含统一回调错误标识和原始异常文本

#### Scenario: 公开表面不得泄漏回调内部令牌

- **WHEN** 检查生成的公开 C# 回调注册方法签名与表面对象
- **THEN** 公开签名 MUST 只出现托管委托和表面对象，MUST NOT 出现 `ulong registration`、`void* user` 或函数指针参数

#### Scenario: 托管桥解回 user 是既有语义而原生层不解引用

- **WHEN** 检查生成的 C++ thunk/代理与托管桥对 user 令牌的处理
- **THEN** C++ 原生生成代码 MUST 只把 user 原样传给函数指针（不解释不解引用）；托管桥 MUST 经 `GCHandle.FromIntPtr(user).Target` 解回托管对象（与现有 OnSetKeyCallback 同语义）；原生层出现解引用 user 即算不合格