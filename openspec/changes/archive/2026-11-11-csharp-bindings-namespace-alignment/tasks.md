# Tasks: csharp-bindings-namespace-alignment

## 0. 开工前置

- [x] 0.1 现场清单：① `__` 双下划线使用点清查——**口径收窄为生成标识符空间**（生成器产物相关代码与绑定符号），排除编译器内建模式（`__int64`/`__VA_ARGS__`/`__declspec`/`__cdecl`）与 Python 双下划线约定（`__init__`/`__name__` 等；全仓含这两类共 95+ 行，属正常）；收窄后非零才停下报主循环定夺改规则还是清障。② CodeGenMain.cpp 短名身份点与 C ABI 命名点逐区清单。③ `check_bindings.py` 短名判据逐条清单（注意 `ushort` 等非判据行不计）。④ 审计表行键结构确认（实测：行键=（接口, 方法）、接口列即短名、无独立短名列）。——重跑命令贴 `evidence\0.1-开工现场.txt`。

## 1. 生成器发射层（D1/D2/碰撞判据，夹具先行红绿）

- [x] 1.1 命名空间镜像（D1）：发射的 C# 类型按完整限定名命名空间部分分层（分段规则 `::`/`_`、首字母大写，根 `Scripting.Generated.Bindings` ）。**改造前第一步**：先跑 1.3 需要的"旧判据拒绝原文"留档（此时旧同短名判据尚在，必红，原文+退出码存入 `evidence\1.3-旧判据留档.txt`）——顺序不可颠倒，先改生成器则该红样永不可复现。**红**=改造前全部类型平铺根下（现状原文）；**绿**=夹具 `ns1::Device`/`ns2::Device` 各自落到镜像命名空间、构建绿。映射后全名冲突夹具（`AB::Widget` vs `Ab::Widget`，层内分段后都映射 `Ab.Widget`）生成期非零点名双方；对照夹具（合法共存）`a_b::Widget` vs `a::b::Widget`（映射 `AB.Widget`/`A.B.Widget`）与 `ns1::Device`/`ns2::Device` 必须绿。
- [x] 1.2 C ABI 槽名线形（D2/D6）：槽名/函数表 struct 名/登记表产物名改 `<ns 段>__<短名>__[__<方法>]` 双下划线线形；注入钩子名同步 `CAInject__<限定名线形>__<M>__<p>`；产物文件名改 `<限定名线形>.Bindings.cs/.cpp`、文件内 C++ 辅助标识符（`thunk_*`/`fromC_*`/`LayoutFieldData`/`Handle`/`Bindings`/错误码枚举等）统一加线形前缀；消费方同步（0.1 实测点位）：build.py 期望产物前缀与共享类型落盘（L1237/L1287）、BindingRegistry 的 include（L12730）、csproj glob（平铺不变）、CoreTests/tester include。**绿**=夹具槽名逐字断言新线形、旧短名槽名 0 命中；同短名双命名空间夹具产物两写不覆盖、同 TU 无重定义（C2011/C2084 为零）。
- [x] 1.3 同短名共存夹具（用户裁定的核心红样）：夹具造 `ns1::Device`/`ns2::Device` 同构建——用 1.1 改造前留档的旧判据非零拒绝原文做对账（复核-0021 指出的顺序死锁已按 1.1 的"改造前第一步"消解），改造后全绿且各自表面/登记表行齐备。

## 2. 判据与治理换源（D3）

- [x] 2.1 `check_bindings.py` 短名判据（0.1 清单点数为准）逐条改限定名口径（编号期望来源仍是锁文件；逐条改前改后入 `evidence\2.1-判据换源.txt`）；**红**=逐条双口径演练：每条判据用旧短名口径跑新产物必须误红/误点名、用新口径全绿——0 抽样，少一条即本条不过。
- [x] 2.2 `check_native_snapshot.py` 对账键改限定名 + 审计表行键接口段改完整限定名（实测行键=（接口,方法）、接口列即短名，直接替换该列，无独立短名列可保留）；**审计脚本同步事实（0.1 实测）**：`ROW_RE`/`SUMMARY_ROW_RE` 的 `[A-Za-z_]\w*` 放不下 `graphics_backend::GPUGraph`，必须同步改（否则 103 行匹配不上）；快照与审计重跑 0 差异。
- [x] 2.3 锁文件表头注释改写：短名列=显示名、身份以限定名列为准；`--update-ids` 的"按限定名识别"核对确认（发号程序不动，限名列已是身份）。

## 3. 全量重建与消费方迁移（D4）

- [x] 3.1 `python build.py --config Debug` 全量重建，产物全部新命名空间/新槽名；构建内 check_bindings 全 0。
- [x] 3.2 消费方逐文件迁移：`CSharpHost/private/CSharpHost.cpp`（34 处 InterfaceId 引用）、`Scripting/Managed/Host.cs`（21 处）、`NativeRenderSugar.cs`、CoreTests、tester 探针、`SurfaceSignatureCheck.cs` 签名断言——全部改镜像命名空间 using 与新常量名。
- [x] 3.3 迁移判据：全仓 grep 旧平铺引用（`Scripting.Generated.Bindings` 后紧跟非命名空间段）与旧短名槽名（如 `CRenderBackend_AddPass`）**0 命中**，逐条豁免（若有）入档；CoreTests 绿、两后端 `--list` 16 条。

## 4. ABI 与换锚（D5）

- [x] 4.1 ABI v10→v11：`kEngineApiVersion` 生成常量单点 +1，`EngineApi` 布局按新命名重建；同步清单四处（对抗验证补漏）：`CSharpHost/README.md`、`Tools/check_csharp_host_failures.py`、`Tools/check_csharp_host_readme.py` + `Tools/CodeGen/check_bindings.py`——后者实测硬编码 EngineApi 期望（`kEngineApiVersion = 10u`、`AbiVersion = 10`、`sizeof(EngineApi) == 56u` 布局映射，L1650-1725），漏改即构建非零；五条失败场景一条不少、期望当场实测重算。
- [x] 4.2 基线换锚（复用批 B 6.1 程序）：旧锚（191 条）封存入 evidence→新锚重取→差异逐文件指认（预期全部产物变化）→check_binding_baseline.py 重指向→"跳过封存被拒"演练红绿留档。

## 5. 全量回归与文档

- [x] 5.1 全量：`python build.py --config Debug`；两后端 `--headless 10 --report` 各 16/16；CoreTests 全绿；check 家族全跑（`check_hash_sources.py` 按既有记名豁免口径）；`openspec validate` strict+all。读数入 `evidence\5.1-全量回归.txt`。
- [x] 5.2 文档换面：`Tools/CodeGen/README.md`（命名空间镜像规则与线形槽名入能力/限制清单、示例输出换真实新读数）、`CSharpHost/README.md`（编号表与点名示例换限定名）、`docs/testing.md`——**内容判据**（对抗验证补漏，防"脚本过、内容没写"）：镜像映射规则与 `__` 线形槽名在 `Tools/CodeGen/README.md` 正文逐字可 grep、编号表条目用限定名，缺即非零；`verify_readme.py`、`check_csharp_host_readme.py` exit 0，before/after 入 `evidence\5.2-文档换面.txt`。

## 7. 返工（用户 2026-11-10 当场裁定：补 `CreateGPUBuffer` 绑定，禁止另立新 change）

- [x] 7.1 补 `CreateGPUBuffer` 直绑（照 `GPUTexture` 先例，无新机制）：① `Interface/RenderInterface/header/GPUBuffer.h` 的 `GPUBuffer` 类挂无参 `CA_BIND`（第六原生接口，经 `--update-ids --config Debug` 发 8 号，段内连续核对）；② 解 `CRenderBackend.h` 的 `CA_BIND_SKIP(CreateGPUBuffer, …)`，`shared_ptr<GPUBuffer>` 返回走批 A"已绑接口按对象登记"；③ 糖层加 `CreateBuffer(GPUBufferDescriptor, EBufferUsageFlags)` + `IDisposable` 保活语义（镜像 `shared_ptr` 引用计数，Dispose 释放托管引用、MUST NOT 提前释放仍被图/后端持有的原生对象）；④ 审计表 `CreateGPUBuffer` 行从"本轮未实现"改登记、快照重跑 0 差异；⑤ ABI v11→v12（绑定面换源，`check_bindings.py` 的 EngineApi 期望四处同步——含 `kEngineApiVersion = 11u` 字面量升 12）；⑥ 基线换锚小轮（程序同 4.2）；⑦ README 编号表补第 8 行。**验收（能失败）**：糖层用例/C# 夹具经 `backend.CreateBuffer` 建缓冲、跨两次建图提交复用同一缓冲（证明生命周期跟随后端不跟图——图内 `AllocBuffer` 做不到这条，红样=回退到 AllocBuffer 路径该用例必败）；构建绿、两后端 16/16、CoreTests 绿、check 家族全 0；读数入 `evidence\7.1-CreateGPUBuffer返工.txt`。

## 修订记录（2026-11-09，对抗验证处置：28 主张/28 复核/裁决 blocking 6·misleading 4·矛盾对 4·误报 2）

裁决与复核存 `%TEMP%\adversarial-review-20261109-144653\`（主张、证据、复核-0001..0028、裁决-全量.json）。首轮 workflow 崩于子 agent 并发上限，接力跑完全程；5 个复核回执丢失但文件全落盘，无真缺口。

1. **blocking·碰撞样例错误**（复核 0001/0009/0019/0024）：改前 design D1/tasks 1.1/spec 两处的样例 `a_b::Widget` vs `a::B_Widget` 映射后其实不同名（`A.B.Widget` vs `A.B_Widget`），红样做不出、验收恒绿；改后统一为 `a_b::Widget` vs `a::b::Widget`（都映射 `A.B.Widget`），并补合法共存对照 `a::B_Widget`。
2. **blocking·旧判据留档顺序死锁**（复核 0021）：改前 1.3 要"旧判据拒绝原文"但 1.1 通过前判据已删、证据不可复现；改后 1.1 明文"改造前第一步先留档"，1.3 引用该留档。
3. **blocking·2.1 演练不可失败**（复核 0022）：改前"任一判据演练"无条数输入期望；改后"逐条双口径、0 抽样、少一条不过"。
4. **blocking·5.2 可空手过**（复核 0023）：改后加 README 正文内容 grep 判据。
5. **blocking·审计表无短名列**（复核 0028）：主循环亲核 `check_bindable_audit.py` L256-276 行键=（接口,方法）证实；改前 D3③/tasks 2.2"短名列保留作显示"；改后"接口列即限定名直接替换，无独立短名列"。
6. **blocking·ABI 同步漏 check_bindings.py**（复核 0004/0011/0014/0027）：主循环亲核 L1650-1725 硬编码 `kEngineApiVersion = 10u`/`AbiVersion = 10`/`sizeof(EngineApi) == 56u` 证实；改后 4.1/design D5 同步清单补为四处。
7. **misleading·四处统计失真**（复核 0006/0015/0016/0017/0018）：改前 proposal 写死"170 处/52 处/20 处/3 处/Host.cs 21 处/191 份全量变"；改后全部标注"开工实测快照，权威数字以 0.1/换锚对照重跑为准"，并注明 ushort 等非判据行不计、"全量变"指全部走换锚程序而非每份内容变化。
8. **矛盾对·发号程序不动 vs 各自发号**（复核 0002/0008）：改后 spec 共存场景明写"各自经 `--update-ids` 追加发号（既有锁行一字不动）"——与锁程序不动不矛盾。
9. **矛盾对·D1 唯一拒绝点 vs 主规范残留同短名拒绝**（复核 0010/0025）：**不改，理由**：主规范旧条文在归档前仍生效，恰是 1.3 红样可复现的前提；本批 delta 归档时全文替换该条文，时间语义非互斥。
10. **矛盾对·提案三 Modified vs 一份 delta**（复核 0012）：改前 proposal 列三个 Modified 能力；改后拆为"Modified: csharp-bindings" + "核对后不动（实测 0 处短名引用）"两节。
11. **矛盾对·`__` 零使用 vs 0.1 停工条件**（复核 0005/0013/0026）：复核实测全仓 95+ 行 `__`（含 `__int64`/`__VA_ARGS__`/`__init__`），我的"零使用"事实错误；改后 design D2 依据收窄为"生成标识符空间零使用"、tasks 0.1 清查口径同步收窄（编译器内建与 Python 约定排除）。
12. **falsePositive·两条不改**（复核 0007/0020）：增量身份键自洽；零命中判据现在失败、迁移后通过，可执行。记录备查。

## 修订记录（2026-11-09 深夜，执行 0.1 后按停工规则 2/3 裁定补规）

13. **新增 D6·产物落盘与 C++ 辅助标识符线形**（执行者报 1.1 阻断：产物文件名仍是 `<短名>.Bindings.cs`、同短名双命名空间同构建互相覆盖；`<短名>_thunk_*` 等文件作用域辅助名被登记表单 TU include、同短名必重定义）：改前工件无此规则；改后裁定选线形方案——产物文件名 `<限定名线形>.Bindings.cs/.cpp`（平铺不落子目录；弃目录镜像方案的理由：`extern "C"` 导出符号不能进 C++ namespace，目录方案省不掉线形命名还要引入跨接口限定引用，失败面更大）、文件内辅助标识符统一加线形前缀；消费方同步点按 0.1 实测（build.py L1237/L1287、Registry include L12730、csproj glob 不变）。同步补 spec ADDED 条文一句 + 新场景"同短名接口产物不互相覆盖"、tasks 1.2/2.2 按此更新（含审计 ROW_RE 放不下 `::` 的实测事实）。
14. **D1 映射规则两种读法消歧**（执行者按停工规则 3 报矛盾，主循环亲算复核属实）：修订记录 1 把碰撞样例改成 `a_b::Widget`/`a::b::Widget` 时引入了与 D1 示例口径（`graphics_backend`→`GraphicsBackend`，`_` 在层内分段拼接）矛盾的另一套读法（`_` 当命名空间层级）。裁定取**示例口径**并写死：`**`::` 分层、`_` 在层内分段拼接、层内各词首字母大写相连**；碰撞红样改为 `AB::Widget` vs `Ab::Widget`（层内分段后都映射 `Ab.Widget`），合法共存对照改为 `a_b::Widget`（`AB.Widget`）vs `a::b::Widget`（`A.B.Widget`）。同步改 design D1、spec 三处示例、tasks 1.1。

## 修订记录（2026-11-10，返工裁定）

15. **补 `CreateGPUBuffer` 绑定（返工任务 7.1，三样齐备）**：①溯源=用户 2026-11-10 当场裁定："走 Backend 创建 Buffer 和走 Graph 不一样，Graph 里的 buffer 生命周期跟着 Graph 走"、"你明明 CreateTexture 都做了 binding"、"要么直接改要么加到当前 change，禁止为这种小事出新 change"。②不放松=新增判据只加严（脚本获得图生命周期以外的缓冲创建入口，验收含"跨两次建图复用同一缓冲"红样）。③对照=改前 `GPUBuffer.h` 无 `CA_BIND`、`CreateGPUBuffer` 挂 SKIP（审计记"本轮未实现"）、脚本建缓冲只有图内 `AllocBuffer`；改后第六接口进表（锁 8 号）、SKIP 解除、糖层 `CreateBuffer`+Dispose 保活。定性修正：批 B 把"四个用例不缺口"误记为"功能不缺口"，SKIP 留得轻率，本返工销账。

## 6. 收尾

- [x] 6.1 清理：%TEMP% 本批临时件、无源产物；最终构建复跑对齐 5.1。
