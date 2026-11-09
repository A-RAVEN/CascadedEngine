# Tasks: csharp-native-direct-binding

## 0. 开工前置

- [x] 0.1 逐条通读四份工件与本 design D1–D10；把 `ScriptRenderBridge.h` 28 个纯虚方法（33 个 `virtual` 行、含 5 个虚析构按审计口径不计）、宿主 `kBindingPolicies[]` 桥 5 行、`Host.cs`/`SurfaceSignatureCheck.cs`/`UserScript.cs` 的 `IScript` 引用点（11/16/28 处，实测口径）三张现场清单重跑命令贴入 `evidence\0.1-开工现场.txt`——数字与工件不符时**停下报主循环**，不许带着错账开工。

## 1. 生成器能力先行（夹具演练，全部红→绿闭环）

- [x] 1.1 内存窗参数投影（D2）：夹具方法 `(const void* + 相邻 uint64_t 长度 + 其余标量)`；**红**=改造前该形状被 ② 拒绝（原文留档，不预设退出码）；**绿**=托管 `byte[]` 过界、实现端逐字节一致断言（CoreTests）；配对歧义夹具（两个整型候选）生成期点名红。投影判据与"裸指针不进托管表面"写进 `check_bindings.py` 文本判据。
- [x] 1.2 记录两段式投影（D8）：夹具返回"可序列化记录的非拥有裸指针"与"未命中空指针"两态；**红**=改造前 ② 拒绝原文；**绿**=托管得副本、空命中 `null`、所有权裸指针形态仍拒（红样=指向夹具资源对象的裸指针返回）。
- [x] 1.3 `CA_BIND_INSTANTIATE`（D9）：`CodeGenMeta.h` 加声明宏（no-op，注释写明不改签名不添方法）；夹具模板方法声明两形态；**红**=未声明形态表面 0 命中 + 拼错类型名构建点名；**绿**=两形态出具体槽、托管同名重载分别调用命中各实例（计数断言）。
- [x] 1.4 快照模式适配三新形态并重跑四真实头快照 + `check_native_snapshot.py` 对账 0 差异；对账必须逐条比较采集/排除标记（前置批已知缺陷：脚本只比对方法名多重集、丢弃标记——本批修掉，红样=把一条已知排除项的标记翻成采集后对账必须变红，翻红演练留档）；采集/排除计数变化逐条指认（`ScheduleData` 系、`Find*` 系、`SetValue` 声明形态的去向），入 `evidence\1.4-快照重跑.txt`。

- [x] 1.5 图形状序列化（D11）：夹具记录含值类容器元素（`vector<CA_BIND_VALUE 值类>`）、命名映射容器成员（`map<NameHash, 标量|记录|值类>`）、接口对象引用成员（`shared_ptr<已挂 CA_BIND 接口>`）三形态；**红**=改造前逐条生成期拒绝原文；**绿**=三类元素往返逐字段一致（值类按四字段线形、映射按 count+键值对、接口引用按句柄反查重建）、坏句柄边界拒且实现计数 0、未支持类别（如 `map<int, T>`）生成期点名留档。
- [x] 1.6 返回位置标量容器两段式（D13）：夹具方法返回 `vector<标量> const&`；**红**=改造前拒绝原文；**绿**=count 与拷贝两槽读数一致且逐元素相等、空容器 count=0 不越界、记录容器/含句柄容器形态生成期点名留档。

## 2. 原生进表（执行顺序 2.1→2.2→2.3：审计口径先行，进表当轮才能绿）

- [x] 2.1 审计表换面：`check_bindable_audit.py` 口径改原生绑定接口（4 新类 + 窗口 2 + 过渡期桥 5），汇总数字当场重算重钉；**红**=漏登记新方法点名，**绿**=全表一致。
- [x] 2.2 `CRenderBackend`/`GPUGraph`/`ShaderStruct`/`GPUTexture` 挂 `CA_BIND()`；`ImageHandle`/`BufferHandle` 挂 `CA_BIND_VALUE`（其成员方法 0 采集判据）；`ExecuteGraph` 挂 `CA_BIND_INJECT(..., scheduler)`（参数名以 `CRenderBackend.h` 逐字为准，实测是 `scheduler` 不是 `taskScheduler`）+ 宿主钩子；`GPUGraph::ScheduleData` 模板形态按 D9 声明需要的实例化，其余模板维持排除；`AddPass` 等取 RenderPass 记录图的方法依赖 1.5（D11）三类元素类别。**两个 CA_BIND_SKIP（主循环裁定，审计记"本轮未实现+原因"）**：`ExecuteGraphTimed`（返回 `unique_ptr<IGPUGraphTiming>`，开第五接口要连 unique_ptr 返回一起放门，脚本用例不用计时）、`Readback(std::span<uint8_t>)`（design Non-Goal 点名的 std::span，GPU 查询批另立）。**验收**=构建全绿，重跑快照与独立对账 0 差异，绑定面与重跑快照逐条一致。
- [x] 2.3 锁文件 host 段换面段 A（D10 两段程序前半）：五个原生接口（`CRenderBackend`/`GPUGraph`/`ShaderStruct`/`GPUTexture`/`WindowHandle`）只经 `--update-ids` 追加在桥行之后（8–12），桥 3–7 一行不动、桥头标注不动——**全程零手工数字编辑**；宿主消费的桥常量名与值原样保留。验收=构建可见状态 1..12 连续无跳号无重号（锁校验非零即红）、登记表常量三处一致；锁 diff 全文入 `evidence\2.2-发号.txt`。段 B（删桥行 + 原生重编号 8–12→3–7）在 5.1 执行，见 5.1。
- [x] 2.4 WindowHandle 进表与对账缺口补齐（D12/D13，3.1 裁定的前置）：`WindowHandle.h` 挂 `CA_BIND`（`GetBackbufferDescriptor` 按序列化只读副本、`GetSizeSafe` 按 `uint2` 记录镜像）；`CRenderBackend::GetWindowHandle` 解 SKIP 进表；发射器修隐式公开默认构造不出 `<类>_Create` 的缺陷（`GPUGraph` 按 csharp-bindings 主规范"带默认构造的类 MUST 生成 Create 入口"出 `GPUGraph_Create`）；`ImageHandle` 值类工厂通道补登记 WindowHandle/GPUTexture 对象句柄构造两条原生构造通道（桥 `.cpp:904`/`:933` 同款）；`GPUGraph::GetPassIndices` 解 SKIP 按 D13 出 `GetPassIndicesCount`/`Copy` 对。验收=构建全绿、快照与审计重跑 0 差异、CoreTests 对五个新表面各有一条读数断言。
  **原受阻记录（2026-11-09 03:3x，已裁定，保留备查）**：本条与 5.1 的删除范围冲突 —— `CSharpHost/private/CSharpHost.cpp` 用五个桥编号常量（`:55-59` 策略表、`:364`/`:368-370` 登记与读数），它们由 `--emit-registry` 按锁行生成。按原 2.3（删桥行 + 摘 `CA_BIND`）⇒ 常量消失 ⇒ `CSharpHost` 编不过，而它的桥消费者要到 5.1 才删。裁定=D10 改两段程序（2.3 纯追加、5.1 删除重编号），见修订记录 22。

## 3. 退役对账表（删桥的前置门槛）

- [x] 3.1 产出桥 28 个纯虚方法逐行对账表（33 个 `virtual` 行里的 5 个虚析构不计）（每行=原生等价物"接口::方法"或"0 消费可删"，依据=三托管文件引用点实测 grep 行贴档）入 `evidence\3.1-桥方法对账.csv`；存在"既无等价物又被消费"行 ⇒ 本条不过，后续删除任务全部禁跑。
  **执行结果（2026-11-09 03:5x，表已产出，闸门判不过）**：`evidence/3.1-桥方法对账.csv` 28 行齐（逐个虚析构已排除），三托管文件的引用点逐行贴档（每行 1–30 处 grep 行号）。**28 行里 7 行是"既无等价物又被消费"**：
  `IScriptRenderTarget::GetWidth` / `GetHeight` / `IsBackbuffer`（原生面没有"渲染目标对象"，目标是图附件）、
  `IScriptRenderGraph::GetPassCount`（唯一等价通道 `GPUGraph::GetPassIndices` 本轮按"托管两段式读取未做"SKIP）、
  `IScriptRenderBackend::GetWindowTarget`（`CRenderBackend::GetWindowHandle` 本轮按"shared_ptr<WindowHandle> 不映射"SKIP）、
  `CreateCaptureTarget`（原生面无离屏捕获目标对象）、`CreateGraph`（`GPUGraph` 没有 `<类>_Create` 槽，C# 拿不到图对象）。
  按本条自己的规则：**本条不过，5.1 及后续删除任务禁跑**。这不是表格做不出来，是原生面缺口：要过闸门必须补
  ① `GPUGraph` 的创建入口、② 目标/窗口对象的等价物（2 个值类或接口）、③ `GetPassIndices` 的托管两段式读取。
  三条都要改设计（D1 的四类之外再加形状或加接口），等裁定后再动。
  **重跑结果（2026-11-09 05:1x，2.4 落地后闸门转绿）**：28 行仍齐、列数 6、**"既无等价物又被消费" 0 行**。七行逐条改判（依据同 CSV 的备注列）：`GetWidth`/`GetHeight`→`WindowHandle::GetSizeSafe`（经 `GetWindowHandle`）；`IsBackbuffer`→`WindowHandle::GetBackbufferDescriptor` + 糖层标记；`GetPassCount`→`GPUGraph::GetPassIndices`（D13，糖层取 Length）；`GetWindowTarget`→`CRenderBackend::GetWindowHandle` + `ImageHandle::FromWindowHandle`；`CreateCaptureTarget`→`CreateGPUTexture` + `ImageHandle::FromGPUTexture`（糖层包成目标对象）；`CreateGraph`→`GPUGraph_Create`。闸门放行，5.1 可以跑（段 B 与删除同轮）。

## 4. 四条渲染用例迁移原生面

- [x] 4.1 `UserScript.cs` 四条渲染用例（`TestCSharpTriangle`/`TestCSharpConstantColor`/`TestCSharpStructuredBuffer`/`TestCSharpTextureSampling`）改写原生直绑表面 + C# 糖层（链式、`using`/`Dispose`、上传糖 `ScheduleData(handle, byte[])`）；托管前置校验防线（D6：拒绝清单/提交完整性/元数据经两段式读取）实现并接 `SurfaceSignatureCheck.cs` 判据换面。**验收**=`--test` 逐条跑四条渲染用例两后端全过、拒绝用例"原生执行计数 0"断言在 CoreTests/托管判据可红（撤校验即红的反向样）。
  **执行结果（2026-11-09 05:5x）**：糖层 `Scripting/Managed/NativeRenderSugar.cs`（目标对象 / 图计划 + 提交前置校验 / 参数结构元数据校验 / 上传糖 / 链式 / Dispose）；生成器补值类**全部**对象构造通道、名称通道改精确 `Internal=0`、新增 `Invalid` 态（修掉 WindowHandle→Backbuffer 通道缺失与生成 switch 重复 case 的红）；`SurfaceSignatureCheck` 换面到原生五类 + 糖层；宿主把原生 `CRenderBackend` 登记为编号 8 的默认实例。验收实测：构建 exit 0；四用例 × 两后端 **8/8 pass**；负向矩阵（12 条负向模式 × 2 后端）错误码逐一对上；`Tools/check_csharp_{triangle,constant_color,structured_buffer,texture_sampling}.py` 全部 exit 0 / 问题 0 处；CoreTests exit 0，新增 `[原生表面] Present(windows 句柄构造) 错误码=0 呈现项=1`。拒绝用例红样：撤掉必需参数块校验后 `skip-param` → exit -2147483645（STATUS_BREAKPOINT，报告 0 字节），恢复即绿；计数灵敏度红样 → `executeGraph=1/0`。全量 16 用例两后端在 4.1 收尾时 16/16 pass（顺带修掉一条 2.3/2.4 之后就存在的过期探针：`TestCSharpWindow` 的"未知接口编号"用 9，而 9 已是 `GPUGraph` 的目标类型表，改为 9999）。全文、命令与日志路径见 `evidence\4.1-原生直绑与糖层.txt`。
- [x] 4.2 `TestScriptHost` 行为逐字不变（D7：`IScriptHost` 不在退役面）；`--list` 仍 16 条。
  **执行结果（2026-11-09 05:5x）**：`IScriptHost.h` 与 `Host.Tick` 的脚本入口路径零改动；`--test TestScriptHost` 的 `TestScriptHost` 条目 pass（`Tick 回值 1 → 2`、`LastError` 为空，日志 `test_output\TestScriptHost.log`），`--list` 16 条（%TEMP%\cnb-list.txt）。注意 `--test TestScriptHost` 会连带跑 `TestCSharpWindow`（既有分派），它在"前两 Tick 未登记窗口实例"的独立跑法下会按"脚本早建过"分支判红——这是 4.2 之前就存在的跑法耦合，全量跑（先跑过 `TestSimpleTriangle` 的窗口登记）16/16 pass。

## 5. 桥整体退役

- [x] 5.1 删除：`ScriptRenderBridge.h`/`.cpp`、宿主 `kBindingPolicies[]` 桥 5 行与工厂锚（宿主直持原生）、`build.py` 桥输入、CoreTests 桥测试件（其覆盖点若 3.1 标了"有消费"则先迁走）、托管三文件残段；**同一执行轮完成 D10 段 B**：先删桥文件与桥头 `CA_BIND` 标注（桥退出登记表输入），再删锁中桥 3–7 行、把原生 8–12 重编号 3–7（本批唯一受控手工锁编辑），随后验证构建。**验收**=残留判据 `git grep -nE 'IScriptRender|IScriptShaderStruct|IScriptTexture|CScript'`（`IScriptHost` 豁免、archive/docs 叙述豁免）0 命中、构建可见状态 1..7 连续无跳号（锁校验非零即红）、逐条豁免理由入档。
  **执行结果（2026-11-09 05:5x）**：四个桥文件已删（头 / .cpp / 私头 / CoreTests 桥测试件）；宿主删桥 include、`m_renderAdapter` 工厂锚与 `kBindingPolicies[]` 桥 5 行，只登记原生 `CRenderBackend`（编号 3）；`build.py` 删桥生成输入与登记输入；`check_bindings.py` 删桥表面判据、登记表 ④ 改为“窗口两字段接常量 + 五个退役字段装配 0”；审计脚本与审计表删桥五行/28 行（13 接口 119 条 → 8 接口 91 条）；清掉上一轮构建遗留的桥产物。**锁段 B**：host 段 3–7 改为原生五类、`# high water 7`，构建可见状态 1..7 连续。验收：构建 exit 0；两后端全量 **16/16 pass**、CoreTests exit 0；残留命令命中只剩 `CodeGenMain.cpp` 的 EngineApi 字段名表（ABI v9 布局，6.2 删）与两个 README（docs 叙述，5.2 换面），逐条豁免理由入 `evidence\5.1-桥退役与锁段B.txt`。
- [x] 5.2 README/文档换面：`CSharpHost/README.md` 编号表与桥叙述、`Tools/CodeGen/README.md` 三新形状入限制清单反向（已落地项）、`docs/testing.md` 用例实现描述——逐字 before/after 入 `evidence\5.2-文档换面.txt`；`verify_readme.py`、`check_csharp_host_readme.py` exit 0。
  **执行结果（2026-11-09 06:0x）**：`CSharpHost/README.md` 编号表 3–7 换成原生五类、章节改「原生直绑面 + C# 糖层」、四个探针入口签名改成 `ScriptRenderBackend`、计数语义改托管读数行、纹理销毁限制改 `GPUTexture`；`Tools/CodeGen/README.md` 审计示例改「8 接口 / 91 条」、限制清单反向（D2/D8/D9/D11/D13 六项已落地 + 仍不支持项）、**新增两条必写说明**（`--update-ids` 必须带 `--config Debug`；删除只删本头自己的行）；`docs/testing.md` 分层数字 23→24 / 12→14、测试清单与 `PASSED` 22→23、四条 C# 用例描述改读数行口径。逐字 before/after 见证据文件。判据：`verify_readme.py` → 22 条命令 0 不符、exit 0；`check_csharp_host_readme.py` → 问题 0 处、exit 0。

## 6. 换锚与 ABI

- [x] 6.1 换锚程序（csharp-bindings 新条）：全绿构建→旧锚封存→新锚重取→两锚差异逐文件指认换源动作；`check_binding_baseline.py` 重指向；"跳过封存被拒"演练红绿留档。
  **执行结果（2026-11-09 06:0x）**：旧锚 96 条 + 11 条允许新增逐字节封存为 `evidence\6.1-旧锚封存-基线产物哈希.txt`（sha256 与 archive 原件一致）；新锚（桥退役后全量 191 条）见 `evidence\基线产物哈希.txt`；两锚差异逐文件指认：删除 11（桥五类产物 + `render.registry.part`）、新增 95（本批 1.x/2.x 生成器扩围产物）、内容变化 64（句柄表共享头 include + `ERROR_BAD_OBJECT_HANDLE=1011` + 编号重排），全文见 `evidence\6.1-换锚前后对照.txt`；`check_binding_baseline.py` 的当前基线改指新锚（脚本 docstring 与 `docs/testing.md` 同步）。演练：新锚 vs 产物 exit 0（缺 0/不一致 0/多余 0）、`--self-test` exit 0、**旧锚 vs 新产物 exit 1（缺 11/不一致 64/多余 84）**——"跳过封存/不重取锚"被逐文件点名拒绝；篡改一条哈希 exit 1。ABI v10 落地后按 design D4 与本任务同批重取一次当前锚（差异只 6 个登记表产物，见 6.2）。
- [x] 6.2 ABI 递增：`kEngineApiVersion` 按生成常量单点 +1，`EngineApi` 布局按新面重建、size 重算，README/check 脚本三处同步新读数；`check_csharp_host_failures.py` 五场景一条不少、期望按当场实测重算（重算≠减少，逐场景退出码留档）。
  **执行结果（2026-11-09 06:0x）**：`CodeGenMain.cpp` 单点：`kEngineApiVersion` 9u→10u、`kEngineApiSize` 80u→56u、删桥五类五个 `*_interface_id` 字段（API 布局 14 字段/80 字节 → 9 字段/56 字节，`static_assert` 复算），托管镜像 `AbiVersion=10`/`Size=56`。判据族重算：`check_bindings.py` 字段表 14→9、期望 10/56；`check_csharp_host_readme.py` ③ 改为「README 行数 == 编号表项数（7）」+「EngineApi 窗口两字段都在、字段数不超表行数」；`check_csharp_host_failures.py` 第 4 条期望「托管侧期望 10」（自检 10→11）；`CSharpHost/README.md` / `Tools/CodeGen/README.md` / `docs/testing.md` 三处读数同步。实测：生成头 v10/56/9 字段；`check_csharp_host_readme.py` 问题 0 处；`check_csharp_host_failures.py` **5 场景 5 通过**（第 4 条退出码 3、文本含「ABI 版本不符」「托管侧期望 10」）；两后端全量 16/16、CoreTests exit 0。重锚：中间锚封存 `evidence\6.2-ABI落地前-基线产物哈希.txt`，当前锚重取（差异仅 6 个登记表产物，预中间锚对当前产物 exit 1 证明 ABI 改动被抓），全文与日志路径见 `evidence\6.2-ABI-v10与重锚.txt`。

## 7. 全量回归与收尾

- [x] 7.1 全量：`python build.py --config Debug`；两后端 `--list` 16 全过 + 四探针基线/增量；CoreTests 全绿（计数按清单实测）；check 家族全跑（`check_hash_sources.py` 按批 A 遗留账基线红豁免口径，其余 exit 0）；`openspec validate` strict+all。每层日志路径+读数入 evidence。
  **执行结果（2026-11-09 06:2x）**：构建 exit 0；`--list` 16 条；两后端全量各 16/16 pass；四探针正常 + 负向共 32 份报告按 check 脚本约定重跑，四份 `check_csharp_*` 全 exit 0 / 问题 0 处；CoreTests exit 0（23 PASSED / 0 FAILED）；check 家族 16 个调用：除 `check_hash_sources.py` 的**记名豁免红**（`remove_cvref_t`，`docs/TODO.md` 2026-11-05 已登记基线与处置，三件相关物本批零改动）外全部 exit 0；`openspec validate --strict` exit 0、`--all` 54 passed / 0 failed。五头快照重跑（119 采集 / 10 排除 / 独立清单 129 条）并过 `check_native_snapshot.py`。逐层日志路径与读数见 `evidence\7.1-全量回归.txt`。
- [x] 7.2 `docs/TODO.md` 直绑批账行处置：本批归档时删除该账行（改完即删），`git grep -n 原生直绑 -- docs/TODO.md` 0 命中贴档。
  **执行结果（2026-11-09 06:2x）**：删除 `[2026-11-06] 原生直绑…` 整条（标题 + 状态行，共 4 行）；`git grep -n 原生直绑 -- docs/TODO.md` → exit 1 / 0 命中；`docs/TODO.md` 其余跨 change 账（含 `check_hash_sources` 的 `remove_cvref_t` 那条）原样保留。
- [x] 7.3 清理：`%TEMP%` 本批临时件、无源产物；最终构建复跑对齐 7.1。
  **执行结果（2026-11-09 06:1x）**：桥遗留产物 10 个 + `render.registry.part` 已于 5.1 删除（构建目录现无 `IScript*` / `ScriptRenderBridge*` 文件）；`.review_tmp/` 已删；`%TEMP%` 本批 198 个 `cnb-*` 临时件逐个删除、0 失败、剩 0（关键读数已逐字抄进各证据文件）。无源产物核查：产物集合 191 条与当前锚逐字节一致（缺 0 / 不一致 0 / 多余 0）。最终构建复跑：exit 0、BUILD SUCCESSFUL（configure 0.0s / codegen 1.3s / 编译 6.2s / 托管侧 1.4s / 合计 53.0s），构建后再对锚仍 0/0/0（确定性成立）。`test_output/`（四个 check 脚本的直接输入）与 `_build_run.bat` 判定保留，理由入档。全文见 `evidence\7.3-清理与最终构建.txt`。

## 修订记录（2026-11-08，对抗验证 13 条裁决处置，逐条改前改后）

裁决原文与复核清单存 `evidence\对抗验证裁决.json`、`evidence\有效复核清单.txt`。blocking 9 条 / misleading 3 条 / falsePositive 1 条，互斥对 0。另记缺口：互相矛盾攻击面回执缺失（0 主张），该面本轮无产出，下批流程需查攻击 agent 是否未启动。

1. **blocking·私头未点名**（proposal:11 挂错 .cpp、tasks 5.1 漏删 325 行私头）：改前 proposal 删除清单只列桥 .h/.cpp；改后补入 `Interface/RenderInterface/private/ScriptRenderBridge.h`（5 个 `CScript*` + 28 个 `*ForTest` 钩子声明所在）与三个消费者（`CSharpHost/private/CSharpHost.cpp`、`Test/GPUBackendTester/Main.cpp`、`Test/CoreTests/ScriptRenderBridgeTest.cpp`），"28 钩子"改挂私头。
2. **blocking·排除面数字互斥**（Non-Goals 写 14 条 vs 实测 16 vs D1 16 减 4）：改前 design Non-Goals"快照排除面 14 条维持排除"；改后"快照排除面 16 条的处置分三类记账：D2 移出 ScheduleData 系、D8 移出 Find* 两条、D9 移出模板 SetValue，其余维持排除，实际数以 D1 快照重跑输出为准"。说明：裁决建议写死 12（仅按 D1 字面 ScheduleData 4 条），但 D8/D9 同样移出条目，写死 12 会与 D8/D9 冲突——按"重跑快照为准"收口，数字唯一来源是工具输出。
3. **blocking·残留判据永不达标**（`IScript|CScript` 匹配命中保留的 IScriptHost）：改前 design Goals"全仓 IScript/CScript 符号残留 0"；改后按名收窄为 `IScriptRender*`/`IScriptShaderStruct*`/`IScriptTexture*`/`CScript*`，明文豁免 `IScriptHost`（D7）与 archive/docs 历史叙述行，与 tasks 5.1 残留判据完全一致。
4. **blocking·INJECT 参数名错**（taskScheduler vs 实测 scheduler）：改前 tasks 2.1 `CA_BIND_INJECT(..., taskScheduler)`；改后 `CA_BIND_INJECT(..., scheduler)` 并注明以 `CRenderBackend.h` 逐字为准（实测 ExecuteGraph/ExecuteGraphTimed 参数名是 `scheduler`，生成器按名匹配、写错即构建红）。
5. **blocking·快照对账丢标记**（脚本只比方法名多重集）：改前 tasks 1.4 对账 0 差异；改后补"对账必须逐条比较采集/排除标记（前置批已知缺陷），红样=把已知排除项标记翻成采集后对账必须变红，翻红演练留档"。
6. **blocking·锁文件跳号死锁**（2.2 留桥行自 8 发号、5.1 再删 3–7）：改前 tasks 2.2"桥 5 行先不删，原生名追加（只增不改）"、5.1 删锁行；改后新增 design D10（锁槽位原地置换：同一执行轮先删桥 3–7 槽、四类原生补 3/4/5/6，段内从 1 连续不跳号，红态=跳号重号锁校验非零），tasks 2.2 改为整轮置换并要求同步摘除桥头 CA_BIND 标注（否则后续构建把桥接口当新登记重新发号、连续判据锁死），5.1 删除清单移除锁行句；proposal L9/L26 同步指向 D10；native-render-binding spec 补 D10 条文。
7. **blocking·两段式例外与主规范 MUST NOT 互斥**：改前 bindable-surface 增量只 ADDED 两条新要求、主规范②"返回位置 T* 维持不可绑"未动；改后增量改为对主规范 Requirement"四类不可投影的形状必须被点名"的 MODIFIED 全文替换——② 参数位置开两条通道（INJECT + 内存窗配对）、② 返回位置开唯一例外（可序列化记录两段式，空指针得 null）、① 开唯一通道（CA_BIND_INSTANTIATE），未经通道合法化的形状维持不可绑，并点名审计表换面时 Find* 两行重归类、Release/Initialize 维持 ④。
8. **blocking·Release 暴露与审计 ④ 冲突**（csharp-rendering 增量"Release 按采集面直接暴露"）：改前该句保留；改后删暴露句、改为"`CRenderBackend::Release`/`Initialize` 维持审计判定的 ④ 语义越界不可绑（CA_BIND_SKIP，不进绑定面，脚本侧释放走托管 Dispose 防线）"；proposal L9/L20 同步补此维持声明。
9. **blocking·SetData 符号不存在**：改前 proposal 字节通道句写"桥的字节数组通道（`SetData(name, byte[])`…）"；改后改指 `IScriptRenderGraph::AddBuffer(name, descriptor, byte[])`（`ScriptRenderBridge.h:87` 声明、`.cpp:459-462` memcpy 后经原生 ScheduleData 提交、托管面 `IScriptRenderGraph.Bindings.cs:316`），全仓无 SetData 符号的实测结论入修订依据。
10. **misleading·裁定出处行号**：改前 proposal L34 与 design Context"两条裁定在 ROADMAP 88/90 行"；改后"直绑与合并接口两条裁定同在第 88 行；废除手写编号裁定在第 90 行"（逐字核实：两句原话都在 88 行）。
11. **misleading·33 virtual vs 28 纯虚**：改前 design D5/tasks 0.1/3.1/native-render-binding 对账条"桥 33 个 virtual 方法"；改后"28 个纯虚方法（头文件共 33 个 virtual 行、含 5 个虚析构按可绑审计口径不计）"；design Context 的"33 个 virtual"为现场实测行数、保留不改。
12. **misleading·六条渲染用例**：改前 proposal/design/tasks 称六条渲染用例；改后四条渲染用例（TestCSharpTriangle/TestCSharpConstantColor/TestCSharpStructuredBuffer/TestCSharpTextureSampling），两条窗口用例（TestCSharpWindow）本不走桥、明文不动。
13. **falsePositive·版本常量互斥**：复核-0017 判定两条 MUST 互斥不成立——场景的"生成接口表版本 TableVersion=2"与正文"kEngineApiVersion v9→v10"是两个独立常量。**不改工件**，依据写入本条。

## 修订记录（2026-11-09，执行中 2.1 受阻裁定，主循环亲核 GPUGraph.h 后定）

14. **D11 新增·图形状序列化**（执行者报 RenderPass/DrawCallBatch/DrawCall 含值类容器、map、shared_ptr 成员，序列化块过不去）：主循环亲读 `Interface/RenderInterface/header/GPUGraph.h` 核实——`RenderPass` 私有成员含 `vector<ImageHandle> m_Attachments`（L372）、`ShaderStructDic shaderStructs`（L369）、`vector<DrawCallBatch> m_DrawCallBatches`（L371）；`DrawCall` 含 `unordered_map<NameHash, BufferHandle> m_BoundVertexBuffers`（L162）；`DrawCallBatch` 含 `ShaderStructDic` 与 `VertexInputDescMap`。依据=`csharp-rendering` MODIFIED 条明文要求 C# 经三记录值形态建图提交 + 零回退裁定——`AddPass(RenderPass const&)` 不绑则桥退役后脚本无建图入口，SKIP 全体不是可选项。design 新增 D11（序列化块元素扩三类：值类容器/NameHash 键映射/已绑定接口 shared_ptr 句柄反查；MUST NOT 不放松）；spec csharp-bindable-surface 增量对"含容器的输入记录经序列化块投影"条 MODIFIED 扩类（含三个新场景）；tasks 新增 1.5（夹具红绿）。原生签名零改动。
15. **ExecuteGraphTimed 与 Readback 裁定 SKIP**：改前 tasks（旧 2.1）要求 `ExecuteGraph`/`ExecuteGraphTimed` 都挂 `CA_BIND_INJECT`；改后只 `ExecuteGraph` 挂 INJECT，`ExecuteGraphTimed`（返回 `unique_ptr<IGPUGraphTiming>`）与 `Readback(std::span<uint8_t>)` 挂 `CA_BIND_SKIP`，审计表记"本轮未实现+原因"。依据：开第五接口要连 unique_ptr 返回一起放门（脚本用例不用计时）；std::span 是 Non-Goal 点名的 GPU 查询批形状。
16. **2.x 任务顺序与编号**：改前 2.1 进表/2.2 锁置换/2.3 审计换面；改后 2.1 审计换面/2.2 原生进表/2.3 锁置换（执行者实测：四类一挂标注审计门先拦，非纯虚公开方法无审计行，进表必须与审计换面同轮）。

## 修订记录（2026-11-09，执行 2.2 时按实测扩形状，逐条留对照）

17. **D11 元素类别 3 → 7（扩形状，不是放松判据）**：D11 原文只列三类（值类容器 / NameHash 键映射 / 接口对象引用）。执行 2.2 时真实记录把它撑破，逐条实测后补四类并各自带生成期拒绝或判据：
    - **④ 定长数组 `std::array<记录, N>`**（`RenderPass::m_AttachmentBlendStates` = `std::array<SingleColorAttachmentBlendStates, 8>`）：线形**不带 count**（个数是类型的一部分），元素按记录字段逐项。改前行为=生成期拒绝 `字段 attachmentBlendStates 是集合容器 / 定长数组`。
    - **⑤ 枚举位标志包装 `uenum::EnumFlags<E>`**（`SingleColorAttachmentBlendStates::channelMask`）：规范类型是记录，不特判会去拆壳的私有成员。改前=拒绝 `字段 channelMask 的嵌套记录没有字段（uenum::EnumFlags<EColorChannelMask>）`。
    - **⑥ 值类字段**（`BufferHandle m_Buffer` 等）：三段线形 kind/handle/名称，C++ 按绑定对象表反查（查无句柄 ⇒ 1011）。改前=把它当记录去拆，撞上 `m_ExternalManagedBuffer` 的未绑定智能指针。
    - **⑦ 联合体**（`union GraphicsClearValue`，16 字节）：定长原样字节过界（C# 镜像 `byte[]`，长度不齐当场抛）。改前=拒绝 `找不到记录 GraphicsClearValue 的定义`（联合体不是 structCursors 的成员）。
    设计不放松的部分原样保留：键非 NameHash 的映射、指向未绑定类型的智能指针、返回位置含这些字段 ⇒ 生成期点名拒绝。
18. **`stdlib` 拼写归一**：`castl::string` 的规范拼写会落成 `std::basic_string<char>`（实测 `ComputeBatch::kernelName`）、`std::string_view` 会落成 `std::basic_string_view<char>`（实测 `NameHash::m_NameView`）；两条都补进文本字段/string_view 判据，否则会走"嵌套记录"分支报"没有字段"。改前行为=上面两条原文。
19. **③ 重载判据改口径**：改前只按投影后的 C ABI 形状做键；改后=**ABI 形状 + 值类/序列化块/已绑定接口参数的托管类型后缀**。依据：`ScheduleData(ImageHandle…)` 与 `ScheduleData(BufferHandle…)`、`AddPass(RenderPass)` 与 `AddPass(ComputeBatch)` 在 ABI 面是同一串字节但在托管表面是不同类型，只按 ABI 判会误杀（实测 ③ 拒绝）；而只按原拼写判会漏掉真正的隐式转换重载（CoreTests 的 ③ 夹具当场变红）。两条叠加后夹具绿、真实表面不再误杀——这是**加严**（多判一层托管类型），不是放松。
20. **`CA_BIND_SKIP` 与同名重载的冲突**：改前按方法名跳过所有同名方法；执行 2.2 时 `CA_BIND_SKIP(ScheduleData, …)` 把两条**非模板**内存窗重载一起排掉（D2 的字节上传通道丢失）。改后=生成器新增规则"同名已有可绑的非模板实现 ⇒ 模板形态自动排除并打日志"，GPUGraph.h 里删掉该条 SKIP。审计表不受影响（模板形态本来不计行）。
21. **2.2 的 SKIP 面比任务书多三条**（`CreateGPUBuffer` ×2 / `GetWindowHandle` / `GetGraphStages` / `GetPassIndices`）：实测托管侧不映射 `shared_ptr<GPUBuffer>` / `shared_ptr<WindowHandle>`（原拼写跳过日志：`[托管表面] 跳过 CRenderBackend::CreateGPUBuffer —— 返回/形参里有本轮不映射的形状（原始拼写：castl::shared_ptr<GPUBuffer>）`），两条只读访问器缺托管两段式读取。依据=不改签名、不新增接口，宁可点名"本轮未实现"也不静默丢。风险已写进 5.1：桥退役时 `CreateGPUBuffer`/`GetWindowHandle` 必须补进绑定面，否则窗口/建缓冲的原生路径缺口。

## 修订记录（2026-11-09，2.3/3.1 双受阻裁定，主循环亲核源码后定）

22. **D10 改两段程序**：改前"同一执行轮删桥 3–7 号槽、原生补 3/4/5/6"；改后"段 A（2.3）`--update-ids` 纯追加五个原生接口 8–12、桥 3–7 不动、零手工数字编辑；段 B（5.1）与桥删除同一轮删桥行并重编号原生 8–12→3–7"。依据=实测 `CSharpHost.cpp:55-59`/`:364`/`:368-370` 消费锁行生成的桥常量、消费者 5.1 才删（2.3 删行宿主编不过）；锁规范删除场景（删行⇒构建非零点名）与"段内连续"不变量在任何构建可见状态都保持。五类（含 D12 的 WindowHandle）追加后为 1..12 连续，5.1 后为 1..7 连续。同步改：tasks 2.3/5.1、spec native-render-binding 的 D10 条文。
23. **D12 新增·WindowHandle 进表 + 目标对象留糖层**：改前 `GetWindowHandle` 与三个目标查询 SKIP、3.1 判"原生面无渲染目标概念"7 行红；改后 `WindowHandle`（`WindowHandle.h` 3 个公开虚方法）挂 `CA_BIND` 为第五个原生接口、`GetWindowHandle` 解 SKIP（入参/返回都是已绑接口，既有机制）、`GetBackbufferDescriptor` 按序列化只读副本过界；捕获/窗口目标改为托管糖类（`CreateGPUTexture` 已绑 + `ImageHandle` 对象句柄工厂通道 + 托管记尺寸/backbuffer 标志）。依据=桥 `.cpp:904`（`ImageHandle(handle)`）/`:933`（`ImageHandle(texture)`）实证两条原生构造通道存在；`CreateGPUTexture` 在 `CRenderBackend.h:108` 未 SKIP。同步改：design D1（四类→五类）、proposal L9/L20、spec native-render-binding、新增 tasks 2.4。
24. **CreateGraph 行=发射器缺陷修复，非设计变更**：执行者报"GPUGraph 没有 `<类>_Create` 槽（类里没有显式公开默认构造）"。主循环亲核 `GPUGraph.h:617-618`（`class GPUGraph` 类体 `public:` 段起、无声明构造⇒隐式公开默认构造）+ 桥 `.cpp:424`/`:995` `make_shared<GPUGraph>()` 实证；csharp-bindings 主规范已写"带默认构造的类 MUST 生成 Create 入口"——发射器对隐式默认构造不出槽是缺陷，修复即可，判据不变不加新设计。
25. **D13 新增·返回位置标量容器两段式**：改前 `GetPassIndices` SKIP（3.1 红行之一）；改后出 `GetPassIndicesCount`/`GetPassIndicesCopy` 对（count+定长拷贝），元素限可 blittable 标量/枚举，空容器不越界，记录/含句柄容器维持拒绝。依据=零回退（UserScript.cs:168/323/481/661 的 pass 计数断言）。同步改：design D13、spec bindable-surface MODIFIED 例外句+场景、tasks 1.6 夹具。
26. **主规范残留裁定（apply 完工核验时发现）**：apply 全勾、validate --all 全绿后亲查主规范，`IScript*` 字面残留 3 行——csharp-host L39 命中条已被本批 delta 的 MODIFIED 覆盖（替换文已写"点名对象为换源后的实际登记名"）、csharp-rendering L46 命中条是原三条 MODIFIED 之一，两处归档时自愈；唯 csharp-rendering"纹理与采样绑定必须一次创建可用并在两个后端通过真实像素验收"条整段以 `IScriptRenderBackend::CreateTexture`/`IScriptTexture`/`IScriptRenderGraph::SetPositionTextureStream` 为主语且不在原三条 MODIFIED 内，归档后会留下点名已退役接口的生效要求。处置=该条补 MODIFIED 全文替换（主语改糖层+原生面组合：CreateGPUTexture+ScheduleData+提交/等待，像素期望/负向矩阵/64 位字节数核对/对象同一性/8 份报告对账逐字保留，判据只严不松）。validate strict+all 复绿。

## 返修记录（apply 完工后、应用户要求交付接口时发现）

27. **`CRenderBackend::Release` 漏挂 SKIP（判据未被放松，是代码没跟上裁定）**：修订记录 8/15 与两份 spec MODIFIED 都裁定 `Release`/`Initialize` 维持审计 ④ 语义越界不可绑（`CA_BIND_SKIP`，不进绑定面），但 2.2 挂标注时漏了 `CRenderBackend.h` 里的 `Release()`（同批 `ExecuteGraphTimed`/`RunTestCode`/`Readback`/`CreateGPUBuffer` 都挂了），审计换面按"进入绑定面的公开方法"取数所以没拦。实测证据：`CRenderBackend.Bindings.cpp` 里有 `extern "C" void CRenderBackend_Release(void* self)` 槽、托管表面有 `Release()`。处置=补挂 `CA_BIND_SKIP(Release, "④ 语义越界：销毁整个后端，脚本不该调（沿用 csharp-bindable-surface 审计表既定结论）")`，重建+check 家族+CoreTests+两后端全量回归复绿后重取基线锚（释放槽消失属换面变化，走 6.1 已立的换锚程序小轮）。返修任务见 7.4。

- [x] 7.4 返修：给 `CRenderBackend::Release` 补 `CA_BIND_SKIP`（引文见修订记录 27），重建后 check_bindings/审计/快照/CoreTests 全绿、两后端 `--list` 16 全过、基线锚按换锚程序重取，读数入 `evidence\7.4-Release补SKIP返修.txt`。
