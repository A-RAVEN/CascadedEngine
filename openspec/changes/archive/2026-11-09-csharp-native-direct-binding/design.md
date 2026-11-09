# Design: csharp-native-direct-binding

## Context

前置批归档件给出全部现状事实：绑定面/排除面地图（快照 160 行：4 头、22 类、110 采集、16 排除=②裸指针 9 + ①模板 4 + 映射容器 3，独立清单 126 条对账 0 差异）；生成器已具备链式自返回、非纯虚、重载序数槽、构造工厂、资源引用值类（`CA_BIND_VALUE`）、序列化块、参数注入（`CA_BIND_INJECT`）七类形态。桥面现状实测：`ScriptRenderBridge.h` 33 个 `virtual`、`.cpp` 1152 行；托管消费面三文件——`Host.cs` 11 处、`SurfaceSignatureCheck.cs` 16 处、`UserScript.cs` 28 处 `IScript` 引用。桥的字节上传实现落点逐字（`.cpp` L459–462）：`m_Graph->ScheduleData(bufferHandle, data.data(), static_cast<uint64_t>(data.size()), 0u, true);`——即"byte[] → memcpy 指针 → 原生 `const void*+size` 窗"。约束：用户裁定逐字（`docs/ROADMAP.md` 第 88 行两句；第 90 行是废除手写编号裁定）——不牺牲 C++ 接口易用性、原生唯一绑定源、糖在 C# 侧、桥退役是结论。

## Goals / Non-Goals

**Goals:**
- 快照四类原生 + 两个句柄值类成为唯一绑定源；16 条 tester 用例（含 4 条 C# 渲染用例：`TestCSharpTriangle`/`TestCSharpConstantColor`/`TestCSharpStructuredBuffer`/`TestCSharpTextureSampling`；2 条 C# 窗口用例本不走桥，不动）行为零回退。
- 桥（`IScript*` 5 接口 + `CScript*` 实现 + 1152 行 .cpp + 宿主策略表 5 行 + 托管引用）整体删除，全仓 `IScriptRender*`/`IScriptShaderStruct*`/`IScriptTexture*`/`CScript*` 符号残留 0——`IScriptHost` 宿主入口接口明确不在退役面（D7），`openspec/changes/archive/` 与 `docs/` 的历史叙述行豁免。
- 数据上传通道（`ScheduleData` 系内存窗参数）在原生面等价可得——退役桥不砍功能。
- ABI/函数表/基线换锚程序明确、可失败。

**Non-Goals:**
- 不改任何原生接口签名或为便利加方法（用户钉死"不牺牲 C++ 易用性"=原生面原样）。
- 不做映射容器出参、所有权不明裸指针（快照排除面 16 条的处置分三类记账：D2 移出 `ScheduleData` 系字节窗、D8 移出 `FindShaderStructData`/`FindShaderRootStructData` 两条返回位置指针、D9 移出模板 `SetValue`；其余条目维持排除，移出后的排除面实际数以 D1 快照重跑输出为准）。
- 不动 `IWindow`/`IWindowSystem` 既有绑定；不做 GPU 查询批的 `std::span`。

## Decisions

**D1 绑定面=快照重跑制。** 原生五类挂 `CA_BIND()`（`CRenderBackend`、`GPUGraph`、`ShaderStruct`、`GPUTexture`、`WindowHandle`——最后一类是 3.1 对账裁定的 D12 扩入）、`ImageHandle`/`BufferHandle` 挂 `CA_BIND_VALUE`、`ExecuteGraph` 的 `TaskScheduler*` 挂 `CA_BIND_INJECT`。**ScheduleData 系 4 条（含模板 2）在字节窗投影落地后重跑快照，绑定面以新快照为准**——排除面从 16 减到含 ScheduleData 数据窗之后的实际数，独立对账脚本同批适配。为什么以重跑为准而非手抄快照：快照条文（前置批 spec）已规定"改真实头必须同步快照"，绑定面漂移只认工具输出。

**D2 字节窗参数投影。** 生成器识别固定形状：`const void*` 参数 + 同参数表内相邻的 `uint64_t` 长度参数（配对判据=紧随其后的整型参数；无法唯一配对⇒生成期点名不猜）。投影为托管 `byte[]`（UTF-8 无关，纯字节），thunk `fixed (byte* p = data)` 传 `p + (ulong)data.Length`；其余参数（offset/bool）原样。C# 糖层补便捷重载（`ScheduleData(handle, byte[])` 定 offset=0），生成器不产糖、糖在 `Host.cs` 用户脚本层或生成的 `Calls` 便捷段——与既有"托管表面=逐字节判据"同族管理。**红**：①配对不唯一/长度参数缺失⇒生成期非零点名；②桥 L462 同形状手写样板与生成产物逐字节判据对齐（真实用例上传像素验收兜底）。替代方案（`std::span` 投影）否决：GPU 查询批再立，本批真实见证就是 `void*+size`。

**D3 退役程序=删除+残留零判据。** 一次删净清单：桥头/.cpp、宿主 `kBindingPolicies[]` 桥 5 行与 `CScriptRenderBackend` 工厂锚（`m_renderAdapter = std::make_unique<...>(...)` 改持原生）、桥 5 行锁条目（host 段换面按 D10 原地置换，一次 change 内完成、段内从 1 起连续不跳号——**宿主段新号可能与夹具段 8–9 数字重叠，段独立号空间为既有规范，不避号**）、`SurfaceSignatureCheck.cs` 桥签名段重写为原生面签名判据、`UserScript.cs` 六用例改写原生表面、`Test/CoreTests/ScriptRenderBridgeTest.cpp` 退役并由原生面对账测试接替、`build.py` 绑定输入换原生四头。**残留判据**：`git grep -nE 'IScript|CScript'` 除 `openspec/changes/archive/` 与 `docs/` 历史叙述行外 0 命中（逐条列主循环核）。

**D4 换锚与 ABI 递增程序。** 顺序钉死：先完成绑定面换源且全绿 → `evidence\换锚基线.txt`（旧 96 份清单封存 + 新产物全集重取哈希）→ `check_binding_baseline.py` 重锚。ABI：`kEngineApiVersion` v9→v10，函数表/`EngineApi` 布局按新面重建（`static_assert` 复算 size），`check_csharp_host_failures.py` 期望计数（原"托管侧期望 9"）按新登记面重算——**重算≠放松**：场景一条不少，数字来自当场 `--emit` 实测。
**D5 桥方法对账表。** 退役前先把桥 28 个纯虚方法逐个指认原生等价物或声明"无消费可删"（`ScriptRenderBridge.h` 共 33 个 `virtual` 行，其中 5 个是虚析构、按可绑审计口径不计入对账；依据=托管三文件 grep 实测消费点）；对账表入 evidence，任何一个桥方法既无原生等价物又被脚本消费⇒该用例不过=功能回退，禁止收口。
**D6 前置校验防线迁到托管糖层。** 桥的"提交前拒绝无效图/参数不完整/缓冲未登记"防线（csharp-rendering 拒绝条与参数校验条的既有语义）不随桥消失：改写为**托管提交前置校验**（C# 糖层在进函数表调用前按同一判据拒绝并点名，拒绝时原生执行调用计数增量 MUST 为 0）；`ObjectDisposedException` 防线本就在托管侧，不动。为什么不放原生：拒绝语义依赖"图内缓冲登记表交叉核对"，原生 `GPUGraph` 对非法输入的现行为是断言/崩溃路径而非受控拒绝，把校验塞回原生等于改原生接口行为——违"不牺牲/不改动 C++ 接口"裁定。红态=临时提交一个未 `SetBuffer` 的结构，托管拒绝且原生执行计数 0。
**D7 `IScriptHost` 不在退役面。** 宿主脚本入口接口 `IScriptHost`（`csharp-host` 两层测试条、`TestScriptHost` 用例）与渲染桥无关——退役清单只含 `ScriptRenderBridge.h/.cpp` 的 `IScript*` 渲染五接口与 `CScript*` 渲染实现；误删即全量测试崩，判据=`TestScriptHost` 用例退役前后行为逐字一致。

**D8 校验数据源：元数据两段式投影。** D6 的托管前置校验要有编译元数据（结构字段表、缓冲/纹理/采样器条目）——原生查询面 `FindShaderStructData`/`FindShaderRootStructData` 返回位置裸指针（快照 ② 排除），不能直绑。本批扩一种投影：**返回位置指向"可序列化记录类型"的非拥有裸指针**两段式化——托管侧得到记录的序列化只读副本（返回空指针⇒托管 `null`），复用既有序列化块线形，原生签名不动。为什么不只免校验：原生 `ShaderStruct::Set*` 对未知字段"静默跳过/越界走 `CA_ASSERT`"是 `csharp-rendering` 拒绝条点名要防的路径，校验必须存在；数据只能从引擎出。红态=返回指针无对应可序列化类型⇒生成期点名不猜。前置批 Non-goal"返回位置裸指针维持不可绑"在前批成立，本批按用户裁定（直绑唯一来源）对该形态开可序列化记录这一道口子，映射容器/所有权裸指针仍不可绑。

**D9 模板参写走"显式实例化声明"通道，不给原生加方法。** `ShaderStruct` 的标量/向量字段写入在原生只有模板 `SetValue<T>`（快照 ① 排除），而批 A non-goal 逐字禁止"给原生接口新增任何方法或改任何签名"（本批同样受 ROADMAP 裁定约束）。机制：新标注 `CA_BIND_INSTANTIATE(方法名, 类型列表)`——在声明头文件里以文本声明"要绑的实例化形态"（宏编译期 no-op），生成器按声明逐形态出具体槽（如 `ShaderStruct_SetValue_float3`），thunk 调 `obj->SetValue<float3>(...)`；托管出同名重载。C++ 侧不新增函数、不改签名；模板体在头文件内定义，生成的 thunk 自身即完成实例化（无需动原生 .cpp），若类型组合本身不可编译（如无对应 `operator()` 路径）⇒编译期在生成 .cpp 报错，构建即红。红态=①声明的类型形态编译不过⇒构建点名；②方法名/类型名拼错⇒生成期或编译期点名。替代方案（自动单态化枚举全部用法）否决=脆弱依赖调用点扫描；（加原生非虚重载）否决=违反裁定。红态=①声明了未实例化形态⇒链接非零；②类型名拼错⇒生成期点名。

**D10 锁槽位两段换面：先纯追加、后删除重编号，任何构建可见状态段内连续不跳号。** 3.1/2.3 实测把原"同一执行轮删 3–7 补 3–6"方案顶死：`CSharpHost/private/CSharpHost.cpp:55-59`/`:364`/`:368-370` 消费按锁行生成的五个桥编号常量，桥消费者要到 5.1 才删，2.3 删行会让宿主编不过。新程序分两段：**段 A（2.3）**——原生接口（D12 后共五个：`CRenderBackend`/`GPUGraph`/`ShaderStruct`/`GPUTexture`/`WindowHandle`）只经 `--update-ids` 追加在桥行之后（8–12），桥 3–7 不动，**全程零手工数字编辑**，构建可见状态 1..12 连续；**段 B（5.1）**——与桥文件/`CA_BIND` 标注/宿主消费者删除同一执行轮：先删桥文件与标注（桥退出登记表输入），再删锁中桥 3–7 行、把原生 8–12 重编号 3–7（本批唯一一次受控手工锁编辑，工具校验兜底），随后验证构建，构建可见状态 1..7 连续。不变量=任何构建可见状态段内从 1 连续无洞无重号（锁校验非零即红）；原生编号值在 5.1 变一次，被 6.1 换锚逐字节对账承接。禁止"先删后置空跨轮补号"的原 rationale 不变——空槽状态永远不许被构建看见。键序按锁文件既有排序规则。

**D11 图形状序列化：序列化块元素扩三类，让 RenderPass 记录图可过界。** 实测 `GPUGraph.h`：`RenderPass` 含 `vector<ImageHandle>`（值类容器）、`ShaderStructDic`（`map<NameHash, shared_ptr<ShaderStruct>>`）、`vector<DrawCallBatch>`；`DrawCall` 含 `map<NameHash, BufferHandle>`；`DrawCallBatch` 含 `ShaderStructDic` 与 `VertexInputDescMap`（`map<NameHash, HashObj<VertexInputsDescriptor>>`）。而 `csharp-rendering` 要求 C# 经 RenderPass/DrawCallBatch/DrawCall 值形态建图提交——`AddPass(RenderPass const&)` 不绑则四个渲染用例在桥退役后无建图入口，零回退即破，SKIP 不是可选项。三类新元素类别（在记录嵌套内任意深度生效）：①**值类容器元素**——`vector<CA_BIND_VALUE 值类>`，元素按四字段线形（kind/handle/nameptr/len）逐个重复，C# 镜像字段为值类数组；②**命名映射容器成员**——键为 `cacore::NameHash` 的 `unordered_map`，值限标量/枚举/记录/值类，线形=count + 重复（key 线形, value 线形），C# 镜像为字典；③**接口对象引用成员**——`shared_ptr<已挂 CA_BIND 的接口类型>`（如 `ShaderStruct`），按绑定对象句柄 uint64 过界，C++ thunk 从绑定对象表反查重建 `shared_ptr`，查无此句柄⇒边界拒+错误码，MUST NOT 解引用空指针。**MUST NOT（不放松）**：键非 NameHash 的映射、指向未绑定类型的智能指针、返回位置的映射容器（维持 Non-Goal）、跨进程/跨表对象引用——任一出现生成期点名拒绝，MUST NOT 静默丢字段；字段布局哈希照旧同源覆盖新类别字段。原生签名与实现不动，这是投影扩展不是接口改动。红态：①夹具三类元素往返逐字节/逐字段一致；②句柄查不到⇒拒绝且实现计数 0；③未支持类别（如 `map<int, T>`）⇒生成期点名留档。`ExecuteGraphTimed`（返回 `unique_ptr<IGPUGraphTiming>`）与 `Readback(std::span<uint8_t>)` 维持 `CA_BIND_SKIP`：前者开第五接口要连 unique_ptr 返回一起放门（脚本用例不用计时），后者是 Non-Goal 点名的 std::span——审计表各记"本轮未实现 + 原因"。

**D12 窗口句柄进表，目标对象留在托管糖层。** 3.1 对账 7 行红里的窗口/目标链路（`GetWindowTarget` ×4 消费、`GetWidth`/`GetHeight`/`IsBackbuffer` ×2、`CreateCaptureTarget`）按实测拆成两半：**原生半**——`WindowHandle`（`Interface/RenderInterface/header/WindowHandle.h`，3 个公开虚方法）挂 `CA_BIND` 进表（第五个原生接口），`CRenderBackend::GetWindowHandle` 解 SKIP（入参 `shared_ptr<IWindow>` 是已绑接口按句柄、返回 `shared_ptr<WindowHandle>` 是已绑接口按对象登记，全是既有机制）；`WindowHandle::GetBackbufferDescriptor` 返回 `GPUTextureDescriptor const&` 按"序列化只读副本"过界（与 D8 同族，const& 即非拥有别名），`GetSizeSafe` 返回 `uint2` 纯标量记录镜像，`IsValid` 非纯虚为批 A 既有形态。**糖层半**——"渲染目标"不是原生对象：捕获目标=托管类（`CreateGPUTexture`（已绑，`CRenderBackend.h:108`）+ `ImageHandle` 按 GPUTexture 对象句柄构造的工厂通道 + 托管侧记尺寸与 `isBackbuffer=false`）；窗口目标=`GetWindowHandle` + `GetSizeSafe` + `ImageHandle` 按 WindowHandle 对象句柄构造（桥 `.cpp:904` 同款原生通道）+ `isBackbuffer=true`。桥 `CreateGraph` 行同理：`GPUGraph` 隐式公开默认构造（类体 `public:` 段无声明构造，桥 `.cpp:424`/`995` `make_shared<GPUGraph>()` 实证）按 csharp-bindings 主规范"带默认构造的类 MUST 生成 Create 入口"出 `<类>_Create` 槽——这是发射器缺陷修复，不是设计变更。依据=零回退：四个渲染用例的消费点（UserScript.cs:106/168/218/323/384/481/544/575/661 等）全要活着过退役。

**D13 返回位置只读标量容器的两段式读取。** `GPUGraph::GetPassIndices`（`GPUGraph.h:682`，`castl::vector<uint32_t> const&`）被 3.1 判红（消费点 UserScript.cs:168/323/481/661，用例的 pass 计数断言）。机制=D8 两段式同族的标量变体：出 `<方法>Count` + `<方法>Copy` 一对槽（count 查询 + 定长缓冲逐字节拷贝），托管糖层 `PassCount` 属性读 `GetPassIndices().Length`。**边界不放松**：元素限可 blittable 标量/枚举；记录容器、含句柄/文本的容器、非常量引用返回维持不可绑；空容器 count=0 且 Copy 不得在零长度上解引用。

## Risks / Trade-offs

- [上传通道的内存窗配对判据误伤其它 `const void*` 方法（`AddData`/`GetPtr`/`DataReference::Create`）] → 配对判据+点名单；这些方法维持 `CA_BIND_SKIP` 显式排除并写理由，红样=临时把配对长度删掉后生成期点名。
- [四条渲染用例改写引入行为漂移（像素/时序断言）] → 四探针基线+增量与 `check_csharp_*` 判据族在改写前后各跑一次，diff 读数入档。
- [EngineApi 布局改动波及宿主] → CoreTests 布局断言（既有 14 字段 static_assert 族重算）+ `check_csharp_host_failures.py` 新期望全场景复跑。
- [换锚掩盖本批意外改动旧产物] → 换锚前的全绿构建 + 换锚动作单独成任务、旧账保留在 evidence 可回溯。

## Migration Plan

1. 能力：D2 字节窗投影（夹具先行、红绿闭环）。2. 原生四类+值类挂标注、`--update-ids` 发号、快照重跑对账。3. 四条渲染用例迁移原生面（行为断言全绿）。4. 桥整体删除 + D5 对账表 + 残留零判据。5. 换锚 + ABI v10 + 判据族期望重算。6. 全量回归（两后端 16、CoreTests、check 家族、validate）。每步产物判据独立可红。

## Open Questions

（无——糖的位置、号段重叠、换锚顺序均已在上文定死。）
