## MODIFIED Requirements

### Requirement: 回调必须带保活与注销，异常必须转成错误码

带 `castl::function<void(...)>` 参数的已审计回调方法 MUST 生成真实的注册、调用与注销通路，MUST NOT 再要求用“本轮未实现”排除。C ABI 必须使用与原始回调参数顺序一致的原生函数指针和不透明 `void* user`，并产生内部注册号与注销伴生；注册号、函数指针和用户令牌 MUST NOT 出现在公开 C# 表面。生成的托管注册表 MUST 保活委托，替换、置空和表面对象释放 MUST 同时清理原生注册与托管保活项。原生函数指针 MUST 调到生成的托管桥，MUST NOT 生成空体 thunk 或固定签名的桩。

回调桥 MUST 阻止非主线程直接进入托管委托，并 MUST 阻止回调执行期间修改同一个回调注册。用户委托抛出的异常 MUST 在托管桥内捕获，MUST NOT 穿过原生回调边界；下一次主线程调用 MUST 通过对应接口的异常类型上报统一错误表中的回调错误标识与原始异常文本。`Tools/CodeGen/CodeGenMain.cpp` 的生成期错误表、`Tools/CodeGen/check_bindings.py` 的 `ERROR_CODE_TABLE` 与 `Tools/CodeGen/README.md` MUST 同步包含该标识，判据不得再把合法的回调错误码当成未知错误。

#### Scenario: 未排除的回调方法必须生成真实调用

- **WHEN** 对一个带 `castl::function<void(...)>` 参数的已审计方法生成绑定并触发原生回调
- **THEN** 生成器 MUST 成功，托管委托 MUST 按原参数类型与顺序收到真实读数，且原生实现确实持有由生成代码安装的回调

#### Scenario: 替换和置空必须清理旧注册

- **WHEN** 先注册一个委托，再用另一个委托替换，随后置空并分别触发原生事件
- **THEN** 替换后只有新委托执行，置空后两个旧委托都不再执行，生成注册表中对应保活项 MUST 已删除

#### Scenario: 回调执行期间修改同一个注册必须失败

- **WHEN** 用户委托在执行期间尝试替换或清空它自己
- **THEN** 桥 MUST 返回可诊断错误且不得销毁正在执行的回调，进程 MUST 继续运行

#### Scenario: 用户委托抛异常不得跨边界

- **WHEN** 一个已注册委托抛出异常，随后主线程再调用 `UpdateSystem` 或回调注册方法
- **THEN** 异常 MUST NOT 穿出原生回调边界，后续公开调用 MUST 抛出对应接口异常并包含统一回调错误标识和原始异常文本

### Requirement: 布局清单与启动自检

生成器 MUST 为每个过界结构体同时产出 C++ 与 C# 两份同源常量（`size` + 每个字段 `offset`）。成员可以是标量或另一个纯标量记录时，生成器 MUST 递归产出嵌套类型的清单，并保留嵌套字段路径；遇到字符串、容器、指针、智能指针或自定义所有权成员 MUST 非零退出并点名完整成员路径。生成器 MUST 另提供可由托管启动过程读取的布局清单入口，`Host.Boot` MUST 逐项比对本侧清单与引擎侧清单，不一致 MUST 点名“哪个类型哪个字段”并拒绝继续初始化。任何布局计算 MUST 来自真实 `sizeof` / `offsetof`，MUST NOT 用手算偏移、假设嵌套记录紧凑排列，或把 C++ `static_assert` 当成托管启动自检。

#### Scenario: 嵌套记录也必须逐字段自检

- **WHEN** 对一个包含另一个纯标量记录的结构体生成绑定，并比较两侧布局清单
- **THEN** 嵌套类型、字段路径、大小与偏移 MUST 全部对应；临时改变任一嵌套字段宽度而不重新生成时，启动自检 MUST 失败并点名该字段路径

#### Scenario: 非标量所有权成员仍然拒绝

- **WHEN** 嵌套记录的任意层级出现字符串、容器、指针、智能指针或自定义所有权成员
- **THEN** 生成器 MUST 非零退出并点名该成员，MUST NOT 把它静默摊平成匿名字节

### Requirement: 每个矩阵行必须有 conformance 夹具

生成器 MUST 让投影矩阵的每一行都能指到一个夹具接口（矩阵在 `Tools/CodeGen/README.md` 第十三节，行数与该节的表逐字一致），并 MUST 有脚本按行打印“矩阵行 → 夹具”映射、断言每一行恰好对应一个夹具、且该夹具生成的 C ABI 签名逐字符合矩阵里写的形状。`castl::weak_ptr<T>` 那一行 MUST 是弱句柄正向夹具；`castl::function<void(...)>` 那一行 MUST 也是正向夹具，除签名外还要执行注册、调用、替换、置空和异常断言。四类不可投影的夹具仍然各自独立存在，MUST NOT 因回调变成正向用例而删除。“会抛异常”那一行 MUST 断言“错误码 + 文本”。MUST NOT 用“矩阵行数与夹具数相等”作为判据，也不能只数回调方法名而不触发真实回调。

#### Scenario: 矩阵每一行都能指到一个夹具

- **WHEN** 跑 conformance 脚本
- **THEN** 输出的映射行数 MUST 等于矩阵实际行数，每行恰好一个夹具；“无夹具的行数”与“签名不符数” MUST 都为 0，回调行还必须有真实行为断言

#### Scenario: 回调那一行必须生成并调用而不是被拒绝

- **WHEN** 跑 conformance 脚本里 `castl::function<void(...)>` 那一行
- **THEN** 它 MUST 生成真实注册入口与注销伴生，夹具触发后 MUST 读到预期参数；`expected.txt` 和 README MUST NOT 再把这一行写成“本轮未实现”或“拒绝”

#### Scenario: 回调行为被改成空桩时必须报错

- **WHEN** 临时把回调夹具的原生桥改成不调用托管委托，或把注销改成保留保活项
- **THEN** 对应行为测试 MUST 非零退出并点名失效行为；恢复后 MUST 重新通过

## ADDED Requirements

### Requirement: 回调注册号与资源句柄必须分离

回调注册号 MUST 只用于回调注册与注销，并同时标识接口、方法和本次注册；资源句柄的 `ReleaseHandle` MUST NOT 用来释放回调。注销旧注册号时 MUST NOT 清除后来替换的新回调。

#### Scenario: 旧注册号不能误删新回调

- **WHEN** 安装新回调后使用旧注册号调用注销伴生，再触发事件
- **THEN** 新回调 MUST 仍然执行，旧注册对应的保活项 MUST NOT 被重新建立

### Requirement: 回调中的借用接口对象必须在返回后失效

原生回调参数若是一个已绑定接口的裸指针，托管桥 MUST 用该接口的公开表面对象承载，并保证所有权仍属引擎。借用对象只允许在回调执行期间使用；回调返回后，托管桥 MUST 撤销借用状态，之后调用该对象任意方法 MUST 得到明确失效异常而不是原生悬垂访问。

#### Scenario: 保存回调对象后在回调外使用

- **WHEN** 用户委托保存回调收到的接口对象，并在回调返回后调用它的方法
- **THEN** 调用 MUST 抛出明确失效异常，进程 MUST 不崩溃；回调执行期间的同名调用 MUST 能读到真实对象数据

### Requirement: 默认比较运算符不得阻止纯标量记录镜像

纯标量记录带有编译器默认的比较运算符时，生成器 MUST 仍把它视为数据记录；其他自定义方法 MUST 继续按非数据类型处理。生成器不得再因 `cacore::Rect<float>` 的 defaulted `operator<=>` 而拒绝 `FloatRect` 与 `MonitorInfo`。

#### Scenario: FloatRect 的默认比较运算符不妨碍生成

- **WHEN** 对包含 `cacore::Rect<float>` 的 `MonitorInfo` 生成嵌套镜像
- **THEN** 默认比较运算符 MUST 被允许，两个矩形与 DPI MUST 全部生成；换成一个自定义非默认方法时 MUST 拒绝并点名该类型
