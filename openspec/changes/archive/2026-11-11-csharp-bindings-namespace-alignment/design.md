# Design: csharp-bindings-namespace-alignment

## Context

批 A 为锁文件定的四列制（编号/短名/完整限定名/头文件路径）与快照的限定名采集，使"名"的数据基础早已是限定名；但发射层把短名当成了公开身份（平铺命名空间 + 短名槽名），并由此立了同短名拒绝判据。用户裁定该判据违背"不牺牲 C++ 接口易用性"。本批把公开身份全链改为限定名，C++ 侧从此对命名与同名零约束。实测分布：CodeGenMain.cpp 短名身份点约 170 处、C ABI 命名点约 52 处、check_bindings.py 20 处、CSharpHost 34 处、Host.cs 21 处；锁文件限定名列与快照限定名采集已存在。

## Goals / Non-Goals

**Goals:**
- C# 生成类型的命名空间按 C++ 完整限定名分层镜像；`ns1::Device`/`ns2::Device` 同构建共存且各自成面。
- 身份链（锁语义、C ABI 槽名、登记表常量、审计行键、check 判据、错误点名文本）以完整限定名为唯一身份。
- 现有 7 宿主接口 + 全部夹具接口迁移完成，旧平铺命名空间引用 0 命中。
- ABI v11、基线换锚、两后端 16/16、CoreTests 全绿。

**Non-Goals:**
- 不改任何 C++ 接口的命名空间与签名（C++ 侧零改动、零新增约束）。
- 不改锁文件四列制与 `--update-ids` 发号程序（编号治理不动）。
- 不引入可配映射表（映射规则写死为 D1，配置化另批再议）。

## Decisions

**D1 命名空间映射规则（C++ 限定名 → C# 命名空间路径）。** 取完整限定名的命名空间部分：**`::` 分层**（每层对应一个 C# 命名空间段），**`_` 在层内分段拼接**——层内各词首字母大写、其余小写、无分隔符相连（`graphics_backend` → 单层 `GraphicsBackend`，`ca_bind_fixture` → `CaBindFixture`，`cawindow` → `Cawindow`；多层的 `a::b` → `A.B`），拼在根 `Scripting.Generated.Bindings` 之下；类名保持原样（含类名内的下划线，如 `a::B_Widget` → `A.B_Widget`）。fixture 段同规则（夹具也在自己的镜像命名空间下，宿主/夹具段隔离不变）。**映射后碰撞判据**：两个不同的 C++ 类型映射到同一 C# 全名（命名空间路径+类名相同）⇒ 生成期非零退出并点名冲突双方——这是本批唯一的同名拒绝点，判定在映射之后（替换旧"同短名拒绝"）。红态=夹具造 `AB::Widget` 与 `Ab::Widget`（层内分段各自只剩首字母大写，都映射 `Ab.Widget`）或 `Foo_Bar::Widget` 与 `foo_bar::Widget`（都映射 `FooBar.Widget`）构建必红；合法共存对照=`ns1::Device`/`ns2::Device`（映射 `Ns1.Device`/`Ns2.Device`）与 `a_b::Widget`/`a::b::Widget`（映射 `AB.Widget`/`A.B.Widget`，`::` 分层与 `_` 层内拼接结果不同）构建必绿。

**D2 C ABI 槽名改限定名线形。** 槽名 = 命名空间段与短名、方法名以双下划线 `__` 分隔（`graphics_backend__GPUGraph__AddPass`），单下划线保留给现有多词短名/方法名内部。选 `__` 的理由：分隔符只需在**生成标识符空间**内无歧义——现有绑定产物与绑定相关手写标识符里没有 `__` 线形（编译器内建 `__int64`/`__VA_ARGS__`/`__declspec` 与 Python 双下划线约定 `__init__` 不在此空间，开工判据按此收窄，见 tasks 0.1）；单段内仍允许下划线，兼容 `AllocImage` 这类命名。函数表 struct 名、登记表产物名同规则。宿主注入钩子名（`CAInject_<I>_<M>_<p>`）同步改 `CAInject__<限定名线形>__<M>__<p>`。

**D3 身份链逐点改造清单。** ① 锁文件：四列制不动，短名列语义改为显示名（表头注释改写明"显示用，身份以限定名为准"）；② 登记表常量：`GPUGraphInterfaceId` → `GraphicsBackend__GPUGraphInterfaceId`（命名空间线形+双下划线+短名+InterfaceId），`Tables` 清单行键同规则；③ 审计表：行键接口段改完整限定名（行键=（限定名, 方法），实测现状接口列即短名、无独立短名列，直接替换该列）；④ `check_bindings.py` 二十处短名判据改限定名口径（编号期望对账仍以锁文件为来源）；⑤ `check_native_snapshot.py` 对账键改限定名；⑥ 错误文本与绑定失败点名（csharp-host"按登记表逐项点名"）点限定名。

**D6 产物落盘与 C++ 辅助标识符同线形（执行者 0.1 后按停工规则裁定的补规）。** 产物文件名 = `<限定名线形>.Bindings.cs/.cpp`（`Ns1__Device.Bindings.cs`，平铺不落子目录——选线形不选目录镜像的理由：`extern "C"` 导出符号不能进 C++ namespace，目录方案省不掉线形命名，还要给登记表引入跨接口限定引用，失败面更大）；文件内全部 C++ 辅助标识符（`<短名>_thunk_*`、`<短名>_fromC_*`、`<短名>LayoutFieldData`、`<短名>Handle`、`<短名>Bindings`、`NativeCountC`、错误码枚举名等）统一加线形前缀（`Ns1__Device_thunk_*`），文件作用域零碰撞。消费方同步点（实测）：`build.py` 期望产物前缀与共享类型落盘（L1237/L1287）、`BindingRegistry.Bindings.cpp` 的 `#include`（L12730）、`Scripting.Managed.csproj` glob（平铺目录不变、模式不变）、CoreTests/tester 的 include 路径。**红**=同短名双命名空间夹具同构建产物两写不覆盖、同 TU 无 C2011/C2084。

**D4 迁移程序（一次性，本批内完成）。** 顺序钉死：① 生成器改发射（D1/D2/D3 发射侧）→ ② 全量重建，产物新命名空间/新槽名 → ③ 消费方逐文件改 `using` 与常量名（CSharpHost、Host.cs、糖层、CoreTests、tester、判据脚本）→ ④ 旧平铺命名空间引用判据：全仓 grep `Scripting.Generated.Bindings` 后跟非命名空间段者 0 命中 → ⑤ 文档（README 编号表/示例、docs/testing.md、CSharpHost/README.md）。每一步独立可红：grep 判据、构建判据、16/16 判据。

**D5 ABI v11 与换锚。** EngineApi 字段名/常量名变更 = 绑定面换源，按 csharp-host 换源条 `kEngineApiVersion` v10→v11；同步清单实测补全为四处：`CSharpHost/README.md`、`Tools/check_csharp_host_failures.py`、`Tools/check_csharp_host_readme.py`（csharp-host 换源条三处）+ `Tools/CodeGen/check_bindings.py`（实测硬编码 EngineApi 期望：`kEngineApiVersion = 10u`、`AbiVersion = 10`、`sizeof(EngineApi) == 56u` 布局映射，L1650-1725，漏改即构建非零）。产物基线 191 份全部重命名空间/槽名（内容差异清单以 4.2 换锚对照为准，非每份都内容变化），按批 B 6.1 立的换锚程序执行（旧锚封存、新锚重取、差异逐文件指认、"跳过封存被拒"演练）。

## Risks / Trade-offs

- [映射规则 PascalCase 化出现异义（如 `FooBAR` 段）] → D1 判据只看映射后全名相等，不做大小写还原；异义本身就是不同段，碰撞才拒。
- [双下划线线形撞既有符号] → 全仓 grep `__` 现有使用点入开工清单，非零即改规则或清障。
- [迁移面大、漏改消费方] → D4 第④步 grep 判据 + SurfaceSignatureCheck 编译期签名断言 + 构建即红三重兜底。
- [审计/check 判据换源期假绿] → 判据先改期望来源（锁限定名列）再改产物，顺序倒置必红，演练留档。

## Migration Plan

1. 开工清单与 `__` 使用点清查（0.1）。2. 生成器发射层改造 + 夹具红绿（D1 映射、D2 槽名、碰撞判据）。3. 判据族换源（D3 ④⑤⑥）。4. 全量重建 + 消费方迁移（D4）。5. ABI v11 + 换锚（D5）。6. 全量回归与文档。每步产物判据独立可红。

## Open Questions

（无——映射规则、分隔符、迁移顺序、ABI 与换锚均已定死。）
