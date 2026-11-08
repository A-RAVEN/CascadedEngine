# Tasks: codegen-native-binding-prereqs

> 纪律（沿用已验证批次口径）：`python build.py`（一律 `--config Debug`）与 `GPUBackendTester.exe`、以及对**产品文件的临时变异**（红态构造，如给 `GPUGraph.h` 加一个公开方法）由主 agent 代跑——执行者把命令与期望逐字发来、拿回原始读数才勾条目；CoreTests、`python Tools\...`、`Tools\CodeGen\...`、grep 由执行者自跑。每条判据先红后绿，红绿原始输出入 `evidence\`；计数断言一律基线+增量；**造红态不得删改生成物**（每建重发会自愈，生成物上造红=假绿）；红线用例数固定 16 条——托管表面新判据走"生成物逐字文本对账 + CoreTests 原生计数"，不新增 tester 用例。数字禁手抄：命中数、行数、文件数当场重算贴档。
>
> 基线（apply 第一步逐条复算入 `evidence\基线.txt`，命令与输出贴档）：带号标注现状**分两类数**——已跟踪：`git grep -nE 'CA_BIND\([0-9]+\)'` 实跑（预期含真实头 7 行与注释行，行数不等于 7，以实跑为准）；未跟踪（底座批新夹具头）：`Select-String -Path Tools\CodeGen\BindableShapes.RenderRegistry.h -Pattern 'CA_BIND\([0-9]+\)'`。清零判据不用"全仓搜索 0 命中"（注释行搜不掉、未跟踪文件 git grep 看不见）——用"登记表输入全部通过生成器无号校验 + 带号标注被非零拒绝"的**行为判据**。其余：`GPUGraph.h` `virtual` 命中 0、13 个 `GPUGraph&` 构件；`check_bindings.py` 的 `EXPECTED_BINDING_NUMBERS`/`FIXTURE_BINDING_NUMBERS` 在位；ABI v9、80 字节；`--list` 16 用例、渲染探针 4 个；审计 12 接口 90 方法；底座批 `render-interface-merge` `complete=15`。

## 0. 前置硬门

- [x] 0.1 归档顺序确认：`openspec/changes/render-interface-merge/` 已不在活跃目录（已归档到 `archive/`，贴 `Test-Path` 假 + archive 目录 `Get-ChildItem` 真两条读数）入 `evidence\0.1-前置.txt`。**本条不满足则整批停工上报主循环**——本批 specs 的三处 MODIFIED 以底座批归档后的主规范文本为基线，两批顺序未落定不得开工。

## 1. 编号锁文件（最高优先）

- [x] 1.1 基线落账 + 对账脚本创建：主 agent 跑 `python build.py --config Debug` 绿；先清 `out\build\x64-debug\codegen\bindings\`（含 `fixture\`）中**无源残留**（登记表输入清单里没有的分片/产物，如底座批演练残留件），清除名单与理由入档；再对全集产物取 `Get-FileHash` 写 `evidence\基线产物哈希.txt`；同文件记 `git rev-parse HEAD` **与** `git status --porcelain` 输出（底座批未提交，只记提交号会假装该提交可复现这批产物）。新建仓库常驻 `Tools\CodeGen\check_binding_baseline.py`：输入=基线文件+产物目录，缺失/多余/不一致非零点名；**自检**：删基线里一行→退出 2"判据自身失效"（红，留档）；还原→退出 0。**验收**=脚本两态输出+基线三件齐。
- [x] 1.2 锁文件与读锁（过渡态）：新建 `Tools\CodeGen\interface-ids.lock`（四列数据行 + `# scope host`/`# scope fixture`/`# high water <n>` 注释行；host 段 1–7、水位行 7，fixture 段 8–9、水位行 9——**两水位不同值，不都写 9**）；生成器改为**从锁读号**，过渡态两种标注都收：无号⇒按锁取号；带号⇒必须与锁一致否则非零点名（锁还是唯一事实源，号只是待迁的冗余）。`--update-ids` 新模块：只加新行（该段 max+1、推水位）只删消失名行、既有行一字不动、不产绑定。**正例必须真跑**：夹具段临时登记一个新名 → `--update-ids` → 断言锁新增行号=10、其余行逐字 diff 零、登记表/托管常量/分片三处消费点同号 → 还原并复绿。**验收（红先）**：锁删 `IScriptTexture` 行 ⇒ 构建非零点名（留档）；锁内塞段内重号/跳号 ⇒ 非零；绿=过渡态构建过、`check_binding_baseline.py` 0。
- [x] 1.3 标注去号与判据改源：真实头 7 处 + 夹具带号 2 处 `CA_BIND(n)` → `CA_BIND`；`CACore\header\CodeGenMeta.h` 宏体与注释改写（注释不再含 `CA_BIND(数字)` 形态示例，防"清零搜索"假阴）；`check_bindings.py` 删两张手抄表、改读锁文件（锁缺失=退出 2）。**验收**：`check_binding_baseline.py` 全绿（过渡态产物本就应逐字节不变）；登记表输入全部无号通过；`git grep -nE 'CA_BIND\([0-9]+\)' -- WindowSystem Interface CACore Tools` 剩余命中**逐条列给主循环核**（只允许注释与文档残留，标注行 0）。
- [x] 1.4 带号拒绝启用（单因红态）：此步才把"标注带号"从"须与锁一致"改成为**拒绝项**。**红**：临时复制一份夹具头改回 `CA_BIND(9)` ⇒ 生成器非零点名"编号由锁文件治理、标注无参数"（原始输出留档后删除临时件）；**绿**：正式夹具全过、`check_binding_baseline.py` 0。红态只在临时副本上构造，正式头不受牵连，单因可归。

## 2. 采集扩围

- [x] 2.1 新夹具接线：`Tools\CodeGen\BindableShapes.NativeShapes.h`（含非纯虚 virtual、非虚公开方法、返回 `Self&` 链式方法、纯标量记录静态工厂、默认构造、同名两签重载、一个模板方法）建头并接入 `build.py` 的 `CODEGEN_BINDINGS`（期望前缀 `IBindNativeShape`）与 `Tools\CodeGen\check_bindings.py` 夹具桩计数口径。**验收**：构建日志出现"绑定：IBindNativeShape"且产物前缀检查过；不接线本组全部红绿无效。
- [x] 2.2 非纯虚/非虚采集 + 自返回 void 化（D3/D4）：先红=现生成器该夹具产物缺这些槽（缺槽点名留档）；后绿=各槽存在、CoreTests 直调读到真实计数；链式方法 C ABI 返回 `void`、C# 侧返回同一表面对象，链式两连 ⇒ 原生计数 +2 顺序一致。**同时改 `check_bindings.py`"丢返回值"判据**：豁免"返回自身引用"类（识别=返回 cursor 拼写为该类型 `&`/`const&`），非自身引用的有返回值方法仍按"丢返回值"拒绝。**豁免自身的红绿**：豁免规则没实现时本夹具被点"丢返回值"（红档）；把豁免写宽（任何返回值都豁免）⇒ 既有"丢返回值"拒绝夹具 `IBindRow09` 系复跑必须仍红（防豁免过界）。
- [x] 2.3 静态工厂（**限纯标量记录返回**）与构造 `Create`（D3）：夹具工厂返回纯标量记录 ⇒ 进表；返回含容器记录的形态（见证用 `RenderPass New` 原语雀形）MUST 生成期拒绝点名"返回位置容器"——**不许把被禁形状标成可绑**。带默认构造类出 `<类>_Create` 槽 + C# `Create`（句柄登记、配对释放、释放后调用抛 `ObjectDisposedException`——托管行为以生成 .cs 文本逐字判据核（`check_bindings.py` 表面段加行），异常路径在 CoreTests 的 C++ 句柄表断言等价物核，不新增 tester 用例）。红=改造前无 `Create` 槽（缺槽输出留档）；绿=全中。
- [x] 2.4 重载序数槽（D5）：同名两签（无隐式转换歧义）⇒ 第 1 槽保持现名、第 2 槽 `_2`，extern 无重名，C# 同名重载分别调用 ⇒ 原生两实现计数各 +1 互不冒充；红=改造前重名 extern 或缺一法；隐式转换重载拒绝夹具 `IBindRowOverloadReject` 复跑仍拒（既有行为不动）。

## 3. 新投影

- [x] 3.1 资源引用值类（D7）：夹具值类（kind 枚举 + 按对象句柄/按名两态构造）。红=改造前该形状被发射器拒绝（**实际报错文本与退出码以留档输出为准，不预设"退出码 15"**——15 是类型发射器的码，绑定发射器路径不同）；绿=线形四段过界、托管工厂两通道（按夹具对象句柄构造、按名构造）往返同一性（同实例断言 + NameHash 与原生直构相等）、类型枚举读出与构造通道一致、坏句柄/空名 ⇒ 错误码 + 实现计数 0；值类成员方法未被采集（产物无其槽，grep 留档）。
- [x] 3.2 序列化块记录（D8）：夹具 `PassLike`（公开字段 + 私有字段经 `CA_CODEGEN_FRIEND`，成员含 `castl::vector<标量记录>`、`castl::vector<枚举>` 与文本）。块头三项（`formatVersion=1u` + 类型名 `NameHash` + **字段布局哈希**）。**红（全部 CoreTests 手摆字节流构造，不碰生成物）**：①version 字节 +1 ⇒ 拒绝、实现计数 0；②类型名哈希改错 ⇒ 拒绝点名；③字段布局哈希不符（含"改名同序"例——只靠前两项哈希检不出的那类）⇒ 拒绝点名；④成员含 `castl::unordered_map` 或 `vector<vector<T>>` ⇒ **生成期**非零点名并给恢复路径（真实实例出现时立扩形状批），不留"生成成功调用失败"中间态；⑤非 const 引用出参形态 ⇒ 生成期拒绝（本批只定义输入方向）。**绿**：往返字段级一致（含两层容器长度与元素值）、块头三项与布局清单同源。**真实头形状见证**入 `evidence\3.2-形状见证.txt`，逐字且归属正确：`static RenderPass New(ImageHandle const& color`（4 重载）、`static AttachmentConfig Create(` / `static AttachmentConfig Clear(` / `static AttachmentConfig ClearDepthStencil(`（`AttachmentConfig` 无 New）、`static DrawCallBatch New()`、`RenderPass` 的三成员 `castl::vector<...> m_AttachmentConfigs;`/`m_DrawCallBatches;`/`m_Attachments;`、`HashObj` 成员在 `PipelineDescData`（`cacore::HashObj<CPipelineStateObject> m_PipelineStates;`）——**不 CA_BIND 真实头**，见证仅供直绑批。
- [x] 3.3 注入（D9）：夹具方法带裸指针参数 + `CA_BIND_INJECT(方法, 参数)`。红三条：注入写非指针参数 ⇒ 非零点名；参数名不存在 ⇒ 非零；有注入声明但无钩子声明 ⇒ 装配核对非零。绿：C ABI 与托管表面均无该参数、原生实现收到夹具钩子（`CAInject_<类>_<方法>_<参数>`）返回的哨兵指针（指针值 + 计数双断言）。钩子命名规范写进 `Tools\CodeGen\README.md`。

## 4. 真实头快照与全量回归

- [x] 4.1 快照模式与**独立计数**对账（D10）：`--parse-report` 只解析不产物；对 `CRenderBackend.h`、`GPUGraph.h`、`ShaderStruct.h`、`GPUTexture.h` 各跑一次，快照入 `openspec\changes\codegen-native-binding-prereqs\evidence\原生头投影快照.txt`。判据脚本（仓库常驻，剥注释文本解析出第二份公开方法清单，与快照逐条对账——**两侧不同源**，堵住"同一生成器漏采两边一起消失"）。**红两例**：主 agent 临时给 `GPUGraph.h` 加一个公开方法 ⇒ 独立清单 +1、快照未变 ⇒ 对账点名（还原复绿）；手工删快照一行 ⇒ 点名。另加"独立清单条数 ≥ GPUGraph 13+10"锚。`docs\TODO.md` 直绑批跨 change 账一行（见 5.2 前）。
- [x] 4.2 全量回归（主 agent 跑 exe，执行者读日志）：`python build.py --config Debug` 绿；`check_binding_baseline.py` 0；两后端不带 `--test` 全量 **16 用例**全过（以开工重跑 `--list` 为准）+ 四探针既有条目基线+增量全中；CoreTests 全绿（含本批新增用例，计数按 CoreTests 自己的清单口径写增量）；`Tools/**/check_*.py` 与 `Tools/CodeGen/check_*.py` 重 glob 全家族逐个 exit 0（份数实测）——**唯一豁免（主循环 2026-11-06 裁决）**：`Tools/CodeGen/check_hash_sources.py` 对 `remove_cvref_t`（`Test/CoreTests/GeneratedHashTest.cpp`）的点名，该红 `docs/TODO.md` 2026-11-05 条目已立账为"基线（`1301768`）即红、只留档不改"，被点名文件自批起点 `d7bc1a2` 零改动（`git diff --name-only d7bc1a2 -- Test/CoreTests/GeneratedHashTest.cpp` 空输出）、脚本本身不在本批改动范围；证据须留该条**原始输出**且读数仍是"命中 1 个来源不明的键类型"——命中数变 0 之外的任何变化（多命中/报错/崩）都算 4.2 不过。其余任何一份 check 红，4.2 不过；`check_csharp_host_failures.py` 5/0。**验收**=每层日志路径+关键读数入 evidence。
- [x] 4.3 文档与能力边界同步：`Tools\CodeGen\README.md`——限制清单句**只删已落地的"通用序列化块"一项**（逐字 before/after 贴档：改前句、改后句），`std::span`、容器出参、映射容器、定长数组、联合体保留原文；资源引用是**新增**形状（原清单没有它，不得声称"从清单删除"）；编号来源改锁文件与 `--update-ids`；参数表补 `--emit-registry` 锁源变化与 `--parse-report`。`CSharpHost\README.md` 编号表来源句改写（"编号在 `Tools/CodeGen/interface-ids.lock`，构建核对；发号经 `--update-ids`"），v9=80 读数不动。`docs\testing.md` 计数重 glob。**验收**=三处逐字 before/after diff 入档 + `check_csharp_host_readme.py`、`Tools\CodeGen\verify_readme.py` exit 0 + 本批不做项在 README 仍可见（保留行 grep 命中逐条贴）。

## 5. 收尾

- [x] 5.1 `openspec validate codegen-native-binding-prereqs --strict` 与 `--all` exit 0；清理 `evidence\` 与预期改动之外的一切临时产物（含 `%TEMP%` 用完即删、无源分片不复活）。
- [x] 5.2 直绑批跨 change 账：`docs\TODO.md` 加一行——"原生直绑 + `IScript*`/`CScript*` 退役批：本批归档后**立即立项**，开工输入=`evidence\原生头投影快照.txt` 与本批能力"；`git grep -n` 命中行贴档（一行、不写改法与验收，按文档地图规矩）。
- [x] 5.3 最终编译验证：`python build.py --config Debug`（项目唯一合法构建路径；默认预设是 RelWithDebInfo，本批全程统一 `--config Debug`）跑至成功，失败即修到成功；最后一轮全量读数与 4.2 对齐复核，"一致/差异+解释"写入 `evidence\5.3-最终构建.txt`。

## 修订记录

**2026-11-06 对抗验证轮修订**（裁决 35 条：blocking 20 / misleading 12 / minor 3，全部落地；逐条全文见 `evidence\对抗验证处置.md`）。判据与口径类变更（改前→改后+依据）：

1. 1.3 清零判据：改前"全仓 `git grep CA_BIND(数字)` 0 命中"→ 改后**行为判据**（登记表输入全过无号校验 + 带号被非零拒绝；注释残留逐条列给主循环核）。依据：复核-01/02/14/19/40/41（注释行搜不掉、未跟踪文件 git grep 看不见、"0 命中"在只删注释后也通过）。
2. 基线计数：改前"7+2 处"→ 改后已跟踪（实跑贴档，行数≠7）与未跟踪夹具头（Select-String）分记。依据同上。
3. 水位：改前"两行都 9"→ 改后 host 段 7、fixture 段 9。依据：复核-03/31/37（定义即"该段历史最高号"）。
4. 步骤序：改前 1.2 一步"读锁+带号即拒"→ 改后拆 1.2 过渡态（带号须与锁一致）/1.3 去号/**1.4 启用拒绝**，红用临时夹具副本单因构造。依据：复核-18/39（原顺序绿不可达、红无单因）。
5. 自动发号：新增 1.2 正例（真跑 `--update-ids`：新行=段下一空号、其余行零 diff、三处消费点同号、还原复绿）；新增 1.1 `Tools\CodeGen\check_binding_baseline.py`（含"篡改基线→退出 2"自检）。依据：复核-20/50/33。
6. 快照对账：改前"生成器投影 vs 快照"→ 改后两侧**不同源**（判据脚本独立剥注释文本解析第二清单 + 条数下界锚），红两例（改头/删快照行）。依据：复核-24/58（同源对账漏采恒等）。
7. 2.1 新夹具接线单列一步。依据：复核-34（不接线产物不存在、本组红绿全不可达）。
8. 4.3 README 判据：改前字符串搜索→ 改后逐字 before/after 核、只删真实存在的行（"本轮未实现"搜索改前改后都 0 命中=恒真）。依据：复核-06/15/16/47。
9. specs csharp-bindings：自返回豁免"丢返回值"判据 + 豁免过界仍须红双向判据；静态工厂限"返回纯标量记录"；scenario"锁被篡改即红"→"删行归构建核对、换号归产物哈希"两分法。依据：复核-44/43/45/46/49（互斥、被禁形状标可绑、不可判条款=假绿）。
10. D8/specs 序列化块：块头三项（含**字段布局哈希**，堵"改名同序"检不出）；红全改 CoreTests 手摆畸形字节流并明写"不得篡改生成物造红"；出参方向显式生成期拒绝；"一层容器"改述为"首批见证覆盖界+超界拒绝留恢复路径"（非规范上限、非魔数）。依据：复核-21/22/23/54/55。
11. 见证引文全部当场验真重写（`AttachmentConfig` 无 `New` 有 `Create/Clear/ClearDepthStencil`、`HashObj` 属 `PipelineDescData`、`RenderPass New` 4 重载）；"序列化块统一规则"归属从生效规范改回 README 十三节；"退出码 15"改"以实际留档输出为准"。依据：复核-08/12/13/25/30/42/56。
12. 新增 0.1 归档硬门 + `specs/csharp-host/spec.md` 显式替换底座条 + 5.2 直绑批跨 change 账（原"直绑下一批"无承诺、增量只增不替）。依据：复核-19/26/35/36/51/52。
13. 删除"从锁消失的名字其旧号 MUST NOT 被复用"句（无退役记录不可判；换号检测移交产物逐字节锚）。依据：复核-45/46。
14. 引文回档：设计里适配层注释与用户裁定原话按仓库/ROADMAP 逐字对齐（复核-09/10/57）。
15. 托管侧三条判据落点：生成 .cs 逐字文本判据 + CoreTests 原生等价断言，tester 用例数 16 钉数不动（复核-26）。
16. **执行期裁决（2026-11-06，主循环，执行者按 §3 上报的工件冲突）**：4.2 改前"全家族逐个 exit 0（份数实测）"→ 改后同名判据加**唯一命名豁免**（`check_hash_sources.py` 对 `remove_cvref_t` 的一条点名；证据留原始输出、读数须仍为"命中 1"，任何别的变化仍算不过）。依据：①该红 `docs/TODO.md` 2026-11-05 条目已立账"基线（`1301768`）即红、只留档不改"（提交于批起点 `d7bc1a2`，先于本批存在）；②被点名文件与脚本自批起点零改动（`git diff --name-only d7bc1a2 -- Test/CoreTests/GeneratedHashTest.cpp` 空输出实测，`check_hash_sources.py` 未出现在 `git status`）；③改法不在本批工件点名范围，动它越 §3 实现范围红线；④其余 13 份全 0 的判据一字未松，豁免只缩到一条具名点名且要求读数逐字不变——新红/该条变化仍红，判据可失败性保持。
17. **归档期工件补漏（2026-11-08，主循环，归档子 agent 按 §3 上报的规范互斥冲突）**：归档子 agent 只读实测发现本增量 `specs/csharp-bindings/spec.md` 只有 3 条 ADDED、**缺对主规范旧条「接口编号只在带编号的标注上声明，且既有编号不可变」（主规范 L358，底座批归档写入）的 REMOVED**——照字面同步会让生效规范同时存"必须带号"与"禁止带号"两条互斥条文且 `openspec validate` 不报错。处置：增量补 `## REMOVED Requirements` 段（标题逐字=旧条标题；Reason=用户 2026-11-05 裁定"手写编号是狗屎"+proposal 第 24 行与 design 第 78/82 行本就写明对旧条"显式替换"，属工件漏写；Migration 把旧条四项强制力逐句指认到新条承接，见该段原文）。判据未放松：旧条"唯一/缺失拒绝/不许改号改序/期望对账"四点全部由 ADDED 锁文件条与其 scenario 承接（换号检测归产物逐字节对账）。补漏后 `openspec validate codegen-native-binding-prereqs --strict` 复跑仍 valid。

执行期如再修订，续记本处；产物判据只严不松，改动须与 design/specs 同步核对。
