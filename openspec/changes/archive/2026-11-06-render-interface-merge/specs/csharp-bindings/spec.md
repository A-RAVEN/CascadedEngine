# csharp-bindings 增量

## ADDED Requirements

### Requirement: 接口编号只在带编号的标注上声明，且既有编号不可变
进入宿主登记表的每个接口 MUST 由 `CA_BIND` 标注携带唯一编号（编号写在这份接口头的标注上，与接口声明同处一地）；生成器 MUST 从标注读取编号并把它作为登记表与 `EngineApi` 两侧编号的唯一来源。编号 MUST 全局唯一：同一编号出现在两个接口、或接口进入登记表范围却没有编号时，生成器 MUST 非零退出并点名涉及接口。已登记的编号 MUST NOT 改号、MUST NOT 改序，只许在现有最大编号之后追加新编号；`Tools/CodeGen/check_bindings.py` MUST 维护"编号 → 接口完整限定名"的期望对账表，重生成产物与期望表不符（含改号、删号、换序）时 MUST 非零退出并点名差异。既有不进登记表的夹具头维持无编号 `CA_BIND()` 现状，不受本条约束。

#### Scenario: 缺编号必须拒绝生成
- **WHEN** 一个被纳入登记表发射输入的接口头里，某接口的 `CA_BIND` 标注不带编号
- **THEN** 生成器 MUST 非零退出并点名该接口，MUST NOT 产出一个少这张表的半成品登记表

#### Scenario: 编号重复必须拒绝生成
- **WHEN** 夹具演练让两个接口带同一个编号进入登记表发射
- **THEN** 生成器 MUST 非零退出并同时点名两个接口与重复的编号

#### Scenario: 改既有编号必须被对账抓红
- **WHEN** 把已登记接口（例如编号 7 的 `IScriptTexture`）的标注编号临时改掉后重跑生成与 `check_bindings.py`
- **THEN** 对账 MUST 非零退出并点名编号差异所在接口；还原后 MUST 回到全绿（红与绿两段输出都留档）

## MODIFIED Requirements

### Requirement: 一个接口一份产物，两侧由同一次生成产出
生成器 MUST 为每个被标注为可绑定的接口产出两份接口文件：C++ 侧的 `<接口>.Bindings.cpp`（thunk + 函数表 + 取表函数 + 句柄表样板）与 C# 侧的 `<接口>.Bindings.cs`（包装类 + 句柄结构体 + 布局清单常量）。接口需要的公开记录和枚举可以另外产出按类型命名的共享 `.Bindings.cs`；这些文件 MUST 与引用它们的接口文件来自同一次生成。每个接口的方法集合与顺序 MUST 在两侧完全一致；MUST NOT 允许一侧手改而与另一侧错开。

同一限定公开值类型的共享文件 MUST 具有确定内容。重复生成时内容必须逐字节相同，同名不同限定名或同名不同内容 MUST 生成失败；共享文件不得复制接口内部的 C 镜像、函数表或错误通道。

此外，生成器 MUST 在同一次构建内、按登记表发射输入产出**跨接口**的两份登记产物：C++ 侧宿主登记表（编号清单、`get_interface_api` 路由、`EngineApi` C++ struct 及其装配代码与编译期断言）与 C# 侧 `EngineApi.Bindings.cs`（与 C++ struct 逐字段同批对照的托管 struct、版本/大小/编号生成常量）。登记产物 MUST 与接口产物同样落在构建目录、不进源码树；重跑 MUST 逐字节相同；`build.py` 的期望产物前缀清单 MUST 包含登记产物，缺失即构建失败。宿主与 `Host.cs` 消费登记产物后，MUST NOT 再各自保留手写的编号常量、镜像 struct 或逐接口取表分支。

#### Scenario: 两侧方法集合逐条对齐
- **WHEN** 对一个被标注接口跑生成器
- **THEN** 生成的 C++ thunk 数量、名字顺序 MUST 与 C# 包装里的方法数量、名字顺序逐条相同（比对脚本给出条数与逐条差异）

#### Scenario: 共享值类型不复制接口内部产物
- **WHEN** 一个接口使用公开记录或枚举，另一个接口随后使用同一限定类型
- **THEN** 两个接口仍各有自己的两份接口文件，共享值类型文件 MUST 只含公开值类型且内容一致，不得出现第二份函数表、布局清单或接口私有转换函数

#### Scenario: 逐接口判据不得把共享值类型当成接口
- **WHEN** `Tools/CodeGen/check_bindings.py` 扫描同时包含接口产物和共享公开值类型产物的目录
- **THEN** `check_surface` 与 `check_alignment` MUST 只处理具有对应 `.Bindings.cpp` 的接口，共享值类型 MUST 改走公开内容、哈希和唯一性判据，不得要求它包含接口类或异常类

#### Scenario: 登记产物与接口产物同批生成且可核缺失
- **WHEN** 在一次构建后人为删除登记表或 `EngineApi.Bindings.cs` 产物再重跑 `build.py` 的绑定核对
- **THEN** 期望产物前缀检查 MUST 点名缺失项并让构建失败；正常构建下两侧 struct 的字段与编号 MUST 出自同一份登记产物，`check_bindings.py` 的编号期望对账 MUST 为 0 处差异
