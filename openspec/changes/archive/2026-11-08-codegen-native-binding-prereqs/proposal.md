# Proposal: codegen-native-binding-prereqs（编号锁文件 + 原生形态直绑能力）

## Why

用户 2026-11-05 两条裁定钉死 CodeGen 的交付标准（已记入 `docs/ROADMAP.md` 阶段 4"排期后续方向"）：其一，"我对codegen的要求就是在不牺牲c++接口易用性的前提下直接生成C#binding，你现在写这些iscripting就是在妥协"——原生接口是唯一绑定来源，手搓的 `IScript*`/`CScript*` 适配层整体退役（该层去留是结论，不是待问事项）；其二，"这种要用户自己手写意义不明的 ID 的东西就是狗屎"——接口编号不许人填，**优先级最高，排在剩余绑定批次之前**。但生成器现在没有直绑原生面的能力，全部有实测依据：发射器只收纯虚方法（`Interface/RenderInterface/header/GPUGraph.h` 全文件 `virtual` 命中 0，13 个链式建图构件全是非虚成员，按现行机制一条也进不了表）；含容器的记录（`RenderPass` 一族）走序列化块是生成器文档既有规则（`Tools/CodeGen/README.md` 十三节逐字："统一规则：**元素里含 `string` / 容器 / 指针 ⇒ 一律降到序列化块**，并在生成期日志里逐条说明"），但同文件限制清单逐字含"不支持……通用序列化块"——**规则在文档、生效规范尚未立条文、实现缺席**，本批 specs 增量把它立为规范；`ImageHandle`/`BufferHandle` 值类按 `Interface/RenderInterface/header/ShaderResourceHandle.h` 注释逐字"两个类各有一个 `castl::shared_ptr` 成员，那是工具明令拒绝的形状"处理，没有任何投影；`CRenderBackend::ExecuteGraph` 的裸 `TaskScheduler*` 入参属"② 所有权不明裸指针"，没有注入机制就只能永远不可绑。本批把这些能力补齐并用夹具逐条证明（配真实头部的"形状见证"快照），下一批（原生直绑 + 适配层退役）才能只搬接口、不补机制。**分两批（能力先行、直绑随后）是主循环的工程判断，不是用户批准过的拆分**——直绑批以带触发条件的跨 change 账落在 `docs/TODO.md`（tasks 收尾条），用户如要求并批，在本批授权开工前改并。

## What Changes

- **编号锁文件（用户钉的最高优先项）**：新增入库 `Tools/CodeGen/interface-ids.lock`——数据行四列制表符分隔（编号、短名、完整限定名、头文件路径），段与水位用注释行表达（`# scope host` / `# scope fixture` / `# high water <本段历史最高号>`），与现有 `*.registry.part` 分片的行形状一致；`CA_BIND` 回归**无参数**——标注上写编号成为生成期拒绝项。默认构建只做核对（**段内**编号唯一、从段起点连续、登记表输入接口名与锁内该段名集合一致；宿主段与夹具段各自独立号空间，夹具段永不进宿主路由）；分配编号唯一途径是显式 `--update-ids` 模式：既有名字保持原号、新名字由工具自动取该段水位+1 并回写锁文件，锁的 diff 进人工评审——人始终不接触数字。初值逐字迁移现值：host 段 1–7（水位行 7）、fixture 段 8–9（水位行 9）。改号/调号的检测责任写死：名字集与输入不符由构建核对抓，**编号与名字的绑定漂移由"产物逐字节对账"抓**（换号必然改生成常量）。ABI 保持 v9、函数表保持 80 字节，**本批开始前已存在的全部 `.Bindings.*` 与登记表产物必须逐字节不变**——这是"编号来源换了、产物不许变"的回归硬锚。
- **发射器采集扩围**：收公开非纯虚与非虚成员函数；自引用返回（`T&`）投影为 void（C# 表面返回自身保持链式；`check_bindings.py` 的"丢返回值"判据 MUST 豁免"返回自身"这一类，否则该判据与本条互斥）；静态工厂可采集的范围**限定为返回值是纯标量记录**——返回位置含容器/文本的记录维持拒绝（`RenderPass::New` 属此类，它的托管替代是镜像类型字段赋值，见序列化块条）；带默认构造的 `CA_BIND` 类生成 `Create` 入口；同名不同签名重载出序数槽（C# 侧同名重载）；模板方法维持 ① 类拒绝。
- **两个新投影形状**：①资源引用——`ImageHandle`/`BufferHandle` 型值类投影为 `(kind, 句柄, 名称)` 组合线形（仍是四类线形形状的组合，不新增 C ABI 线形），托管侧工厂通道按值类构造器一一对应（夹具以"按对象句柄构造 + 按名构造"两通道证明；真实值类的接线在直绑批按快照落地——条文对未进登记表的类不触发，不存在"规范已生效却无人验证"的中间态）；②序列化块记录——成员含一层容器（`castl::vector<标量|枚举|纯标量记录>`）或文本的**输入位置**记录出托管镜像（字段级），过界用 `uint8_t const* + count`，C++ 侧经 CodeGen 序列化设施还原栈对象再调原生，**原生签名不动**；"一层容器"的深度界不是拍脑袋魔数：直绑首批真实见证（`RenderPass` 的三个成员 `m_AttachmentConfigs`/`m_DrawCallBatches`/`m_Attachments`）全部是一层 vector，更深容器在生成期显式拒绝并写明恢复路径（真实实例出现时立扩深批）。**出参/非 const 引用位置的容器维持生成期拒绝**（本批不做双向序列化，理由随排除清单留名）。序列化器两侧同源生成。
- **参数注入**：新标注 `CA_BIND_INJECT(方法, 参数)`——把 ② 类**参数位置**裸指针声明为引擎默认提供，thunk 从宿主固定名钩子取值；C# 表面不见注入参数；返回位置裸指针维持不可绑。
- **真实头形状见证快照**：对 `CRenderBackend.h`/`GPUGraph.h`/`ShaderStruct.h`/`GPUTexture.h` 出一份"谁进表 / 谁须排除 / 理由"的解析投影清单入库，并配**独立于生成器**的方法计数交叉核对（用文本解析从头部摘公开方法名清单，与快照条数对账——快照漏采时两侧数字对不上即红，堵住"快照与对账同出一个生成器"的恒等式）；它就是直绑批的开工地图。
- **判据与文档同步**：`check_bindings.py` 的编号对账改读锁文件（手抄 `EXPECTED_BINDING_NUMBERS`/`FIXTURE_BINDING_NUMBERS` 两张表退役）；新增基线哈希对账脚本（仓库常驻、带自检，把"产物逐字节"与"快照对账"从口头承诺变成可执行判据）；两份 README 按实测形态更新——限制清单句里删去已落地的"通用序列化块"一项、其余保留项逐字不动，"未实现清单"没有的东西不许声称删除。

## Capabilities

### New Capabilities

（无——全部并入现有能力。）

### Modified Capabilities

- `csharp-bindings`: 修订底座批引入的"接口编号只在带编号的标注上声明"条（编号来源改锁文件、标注带号变拒绝项、段内核对口径、产物逐字节锚）；新增"发射器采集非纯虚公开方法（自返回 void 化含"丢返回值"判据豁免、静态工厂限纯标量记录返回、构造入口、重载序数槽）"与"真实头投影快照 + 独立计数交叉对账"两条要求。
- `csharp-bindable-surface`: 修订"四类不可投影"（② 参数位置新增 `CA_BIND_INJECT` 合法化通道，返回位置维持不可绑）与"嵌套记录"（含容器/文本的记录改走序列化块而非一律拒绝）两条原文；新增"序列化块记录投影（一层容器为直绑首批见证形状所设界，更深与出参位置生成期拒绝并留恢复路径）"与"资源引用值类往返"两条要求。
- `csharp-host`: 修订底座批"宿主登记与跨边界函数表由生成登记表提供"条中"编号由带编号的 `CA_BIND` 标注声明"一句——改"编号由锁文件声明、构建核对、产物逐字节锚不变"；其余登记/路由/失败语义条文不动。

## Impact

- 生成器与判据：`Tools/CodeGen/CodeGenMain.cpp`、`Tools/CodeGen/check_bindings.py`、`CACore/header/CodeGenMeta.h`（宏体与注释）、`build.py`（登记表链路读锁、`--update-ids` 入口）、新文件 `Tools/CodeGen/interface-ids.lock`、新夹具头（`BindableShapes.NativeShapes.h` 一族）。
- 标注形态迁移（只动标注不动任何签名）：`WindowSystem/header/CAWindow/WindowSystem.h` 两条 `CA_BIND(1)`/`CA_BIND(2)` 与 `Interface/RenderInterface/header/ScriptRenderBridge.h` 五条 `CA_BIND(3..7)` 改无参数；夹具带号标注同上。
- 文档：`Tools/CodeGen/README.md`、`CSharpHost/README.md`、`docs/testing.md`。
- 回归锚（必须不变）：ABI v9/80 字节、全部既有绑定产物逐字节、宿主与托管运行行为、四个渲染探针与 `--list` 全部 16 用例的行为、审计表 12 接口 90 方法口径（`ScriptRenderBridge.h` 的接口一个不动，方法集合不变）。
- 与底座批次 `render-interface-merge`（已实施 15/15、未归档）的关系：顺序已定——**底座批先归档、本批后实施**（本批的三处 MODIFIED 都以它归档后的主规范文本为基线，见 specs 增量；若用户指示不先归档底座，本批不开工——tasks 0.1 是开工硬门）。底座批 delta 里与本批冲突的"编号写在标注上"条文由其归档时的主规范承接、本批增量负责替换，不留两批并存矛盾。
- 直绑批（下一批）的开工条件由本批交付：原生四头的投影快照 + 全部所需形态能力 + 锁文件编号机制；本批收尾条把它登记为 `docs/TODO.md` 跨 change 账（触发条件写死："本批归档后立即立项，快照文件为开工输入"），不留在口头。

## Non-goals

- 不给任何原生接口加 `CA_BIND`、不生成任何原生类的 C# 绑定、不退 `IScript*`/`CScript*` 任何一行——那些是直绑批的全部。
- 不给原生接口新增任何方法或改任何签名（"加原生方法"不在本批与直绑批的射程内）。
- 不做 `std::span` 投影（直绑首批用到的 `ScheduleData` 线形已是 `void const* + size`；span 只在 GPU 查询批的 `Readback`/`IGPUGraphTiming::Passes` 上出现，届时再立）。
- 不做模板单态化绑定（`ShaderStruct::SetValue<T>` 维持 ① 类显式排除，直绑批按快照决定镜像形状）。
- 不做容器出参、`unordered_map`、元素含 `castl::string` 的容器、联合体与定长数组投影。
- 不动 ABI 版本、函数表字节布局、宿主分派语义。
