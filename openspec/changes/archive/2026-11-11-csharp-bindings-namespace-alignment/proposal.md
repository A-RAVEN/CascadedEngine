# Proposal: csharp-bindings-namespace-alignment（C# 绑定命名空间镜像 C++，废除同短名拒绝）

## Why

用户 2026-11-09 裁定（`docs/ROADMAP.md` 已记为最高优先级）：批 A 立的"两个命名空间存在同短名接口⇒生成期非零退出"判据（`openspec/specs/csharp-bindings/spec.md` 同短名拒绝 scenario 及"公开接口短名 MUST 按规则核对"句）是代理自定、未经用户背书的规则，与 CodeGen 定义性要求（用户原话，ROADMAP L88）："我对codegen的要求就是在不牺牲c++接口易用性的前提下直接生成C#binding"直接冲突——C++ 里 `ns1::Device` 与 `ns2::Device` 同名是正常写法，生成器拒绝等于给 C++ 侧强加约束。用户原话："这个规范是你定的不是我定的，而且明显不符合我的要求"、"要改记到RoadMap里，最高优先级"。

现状实测：全部托管绑定类发进一个平铺命名空间 `Scripting.Generated.Bindings`（CodeGenMain.cpp 六处发射点写死，L3010/3033/5033/6540/6560/9030），公开身份链以接口短名为准——锁文件短名列、登记表常量（`GPUGraphInterfaceId`）、C ABI 槽名（`GPUGraph_AddPass`）、审计表、check 判据族（`check_bindings.py` 20 处短名判据、`check_native_snapshot.py` 3 处）。锁文件已有完整限定名列（4 列制），快照采集已记录每个类的完整限定名——治理层数据早已就绪，缺的是全链改用限定名。

## What Changes

- **C# 命名空间镜像 C++ 限定名**：生成类型按完整限定名分层落命名空间（映射规则见 design D1，如 `graphics_backend::GPUGraph` → `Scripting.Generated.Bindings.GraphicsBackend.GPUGraph`），平铺 `Scripting.Generated.Bindings` 退役。
- **身份链全链改限定名**：C ABI 槽名（D2 双线形分隔防拼接碰撞）、登记表常量名、审计表行键、check_bindings/check_native_snapshot 判据、错误文本点名的接口名——全部以完整限定名为准，短名只作显示名。
- **废除同短名拒绝、改判映射后碰撞**：`csharp-bindings` 主规范的同短名拒绝 scenario 与短名核对句 REMOVED，替换为"D1 映射后的 C# 命名空间路径+短名 仍冲突 ⇒ 生成期非零点名冲突双方"（跨命名空间同名从此合法）。
- **现有接口全量迁移**：7 个宿主接口（`cawindow::`×2、`graphics_backend::`×5）+ 全部夹具接口（`ca_bind_fixture::`）的 C# 命名空间搬家，消费方同步：CSharpHost（34 处 InterfaceId 引用）、`Host.cs`（21 处）、糖层、CoreTests、tester 探针、README/docs。
- **ABI v10→v11 + 基线换锚**：登记表布局与常量名变更按 csharp-host 换源条递增 ABI；产物基线走已立的换锚程序（批 B 6.1 程序复用）。

## Capabilities

### Modified Capabilities

- `csharp-bindings`：REMOVED 同短名拒绝 scenario 与短名身份句（MODIFIED 全文替换两条）；ADDED 命名空间镜像与映射后碰撞判据条；锁文件/登记表/审计的"名"改限定名治理。

### 核对后不动（实测 0 处短名引用，不留增量）

- `csharp-bindable-surface` / `csharp-host`：开工 grep 实测两份主规范 0 处"短名"引用（2026-11-09 复核确认），本批不动其条文；host 侧消费代码改动属实现面，进 tasks 不进 spec 增量。

## Impact

- 产品：`Tools/CodeGen/CodeGenMain.cpp`（shortName 身份点约 170 处、C ABI 命名点约 52 处，均为 2026-11-09 开工时实测快照，权威数字以 0.1 现场重跑为准）、`Tools/CodeGen/check_bindings.py`（短名判据约 20 行，含 ushort 等非判据行，0.1 逐条清点）、`check_native_snapshot.py`、`check_bindable_audit.py`（行键接口列）、`Tools/CodeGen/interface-ids.lock`（短名列改显示名列语义，四列制不变）、`CSharpHost/private/CSharpHost.cpp`（InterfaceId 引用约 34 处）、`Scripting/Managed/Host.cs`（约 20 行，0.1 重跑为准）、`Scripting/Managed/NativeRenderSugar.cs`、`Test/CoreTests/`、`Test/GPUBackendTester/`。
- 产物：全部 `.Bindings.cs/.cpp` 重命名空间/槽名；基线 191 份全部走换锚程序（内容差异清单以换锚对照实测为准——非每份都内容变化）。
- 回归口径：`python build.py --config Debug`、两后端 `--list` 16 全过、CoreTests 全绿、check 家族全 0、`openspec validate --all` 全绿；红样=同短名双命名空间夹具构建通过（旧判据下必红）。
- 规范依据：ROADMAP 最高优先级条目（2026-11-09，含用户三条原话）；csharp-bindings 主规范 L350-352 为被废除对象。
