# Tasks: csharp-resource-asset-foundation

> 验收口径：每条给「命令 / 产物 + 期望读数」，期望值写死、不从被测物推导；红态读数存 `evidence/`。
> 构建唯一路径 `python build.py --config Debug`（CLAUDE.md 钉死；模板规则里的 build.bat 在本项目不存在，以项目脚本为准）。
> 故事映射：US1 = C# 自助注册表（§1/§2），US2 = C# 资产与自读加载（§3），US3 = 渲染消费与验收（§4/§5）。

## 1. 接线与编号（US1）

- [x] 1.1 新建门面头 `Interface/ScriptHost/header/ScriptHost/IResourceRegistry.h`：`CA_BIND()` + 九个纯虚方法（design D2 逐字），中文注释写明「委托 ResourceManagingSystem、注册表仍是单一强所有者、门面不持第二份强引用」。验收：`Test-Path` 为真；`Select-String '= 0;'` 计数 = 9（纯虚方法数，排除虚析构——按 'virtual' 数会数到 10）；`CA_BIND()` 命中 1。
- [x] 1.2 `build.py` 的 `CODEGEN_BINDINGS` 与 `CODEGEN_REGISTRIES` 加门面行（期望前缀 `cs_script__IResourceRegistry`）；用 Debug 档 CodeGen.exe 跑 `--update-ids`。验收：生成 exit 0；`interface-ids.lock` host 段高水位 = 13、门面占 13、1–12 行逐字不变（改前快照对照存 `evidence/1.2-锁改前.txt`）；fixture 段高水位仍 14。
- [x] 1.3 `CSharpHost.cpp`：`kBindingPolicies` 加 `{ kBindingInterfaceIdcs_script__IResourceRegistry, true }`；取 `ResourceManagingSystem` 实例 → new 门面实现 → 经 `SetInterfaceInstance` 注册（CRenderBackend 先例 CSharpHost.cpp L356-382）。**注册点 MUST 在 `CreateRenderAdapter` 的渲染后端缺失 early-return 分支之前**（或无条件初始化段）——无渲染后端的宿主也要能取门面，不许静默不登记；门面实现对象生命周期随宿主（宿主持有，与实例表同口径，不悬空不泄漏）。验收（正向，执行者）：构建过；tester 里 `GetInterfaceApiForTest(门面编号)` 返回非 0 且实例非空（断言写进 4.2）。红态读数（**主循环变异**，子 agent 不做变异）：临时去掉 kBindingPolicies 门面行重建 → GetInterfaceApi 返 0，用例红在 `E_ASSET_FACADE: 取不到门面函数表（编号 13）`（实测 Unregistered 分支静默返 0、不打文本——原期望「没有在宿主角色登记表里登记」文本不存在，按实测改判据；红态仍成立：缺行 → 取表失败点名编号），存读数后恢复该行重建转绿。
- [x] 1.4 审计表追加（唯一一份：`openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`，只追加不动旧行）：主表九行（逐行写投影形状 / 分配归属 / 错误通道）+ **顶部汇总表一行**（`cs_script::IResourceRegistry` | 9）+ **合计 105→114** + **范围段改 14 个接口 / 114 条方法并加门面名** + **「谁实现」表一行**（门面由 C++ 宿主实现；该表要求每接口恰好一行）。脚本 summary_rows 逐接口核汇总行、合计与实算对账，缺一项即红。验收：主表区 `Select-String 'IResourceRegistry'` 计数 = 9、汇总表含门面行、合计 = 114、范围段含 14/114、谁实现表含门面行；脚本绿须与 1.5 同步完成（单独追加主表行会因僵尸行红，见 1.5）。
- [x] 1.5 `Tools/CodeGen/check_bindable_audit.py` 的 `INTERFACES` 表加 `"cs_script::IResourceRegistry": ("Interface/ScriptHost/header/ScriptHost/IResourceRegistry.h", "IResourceRegistry")` 一项。验收：`python Tools/CodeGen/check_bindable_audit.py` exit 0，输出接口数 13→14、方法对账数 105→114（基线实测存 `evidence/1.5-审计基线.txt`；1.4 的汇总行与合计须已同步，否则脚本报「汇总表缺少接口 / 合计不符」）；红态读数用自然时序取：1.4 已追加而本条未做时跑脚本 MUST 非零退出并把九行判僵尸行（先存该红态读数，再做本条转绿）。
- [x] 1.6 `CSharpHost/CMakeLists.txt` 与 `Test/GPUBackendTester/CMakeLists.txt` 的 include 目录各加 `${CMAKE_SOURCE_DIR}/Interface/ScriptHost/header/ScriptHost`（生成产物按基名 `#include "IResourceRegistry.h"` 找头，与 CAResource 行同类先例；tester 要 include 门面产物，同一理由——执行侧 apply 阶段补入）。验收：构建过；红态读数自然取：未加该行时产物编译报 C1083 找不到头（存读数后加行转绿）。
- [x] 1.7 生成器改动①：门面专属错误码 1014（design D2/D7）：`CodeGenMain.cpp` 错误码表加 `{ "RESOURCE_OPERATION_REJECTED", 1014 }` + 作用域过滤（只发射进 `cs_script__IResourceRegistry` 产物，照 `isReverseOnlyErrorCode` 先例）；`check_bindings.py` 的 `ERROR_CODE_TABLE` 加 1014；`Tools/CodeGen/README.md` 错误码表加 1014 行、形状表「会抛异常的方法→生成器 try/catch」行改准（普通方法 thunk 无 try/catch，错误由实现写通道）。验收：生成 exit 0；门面产物含 `ERROR_RESOURCE_OPERATION_REJECTED = 1014`；既有正向产物不含 1014 且逐字节不变（5.1 覆盖）；`check_bindings.py` 绿；README 含 1014（grep）。
- [x] 1.8 生成器改动②：字符串两段式返回带参数（design D7）：`CodeGenMain.cpp` 的 stringTwoStep 分支把方法参数带进 Len/Copy 伴生（C++ 签名 / 表槽 / C# delegate / 调用壳都带参；thunk 按 castl::string/string_view 重建实参）；不支持的参数形状生成期点名拒绝，不静默丢参；**零参数方法生成字节与旧实现逐字相同**。验收：门面产物 `GetResourceFullPathLen(void* self, char const* path, uint64_t pathLen)` 与 `CopyGetResourceFullPath(void* self, char const* path, uint64_t pathLen, char* dst, uint64_t cap)`（grep 逐字）；生成 exit 0、构建过（C2660 消失）；既有产物逐字节不变（5.1 覆盖，零参数字节同构是依据）。
- [x] 1.9 配套判据修正：`check_bindings.py` 的「句柄泄漏」判据豁免——接口头里**字面写成 `uint64_t <名>`** 的参数（design D2 的合法标量句柄，如 `factoryHandle`）不判泄漏；`shared_ptr` 投影出来的句柄（头里拼写是 shared_ptr）照旧判红。验收：`python Tools/CodeGen/check_bindings.py --product-dir out/build/x64-debug/codegen/bindings` exit 0；豁免仅限字面 uint64_t 拼写（grep 判据代码）；其余判据零变化。

## 2. 门面实现（US1）

- [x] 2.1 实现九方法（design D1/D2/D3）：委托注册表；**实现自行 catch** 委托抛出的异常（`resource_creation_error` / `io_error` / `ReverseBindingError`），调生成 `setError` 写错误通道——句柄解析空 → 1001，其余操作失败（表缺失 / 未注册类型 / Create 失败或返空 / 同路径 / IO 错误）→ 1014，文本点名原因（正向 thunk 无 try/catch，实测；C# 包装自动 ThrowIfFailed）；`RegisterFactory` 经 `ca_bind_registry::GetInterfaceApi` 取反向表 → `ResolveImpl` → 委托句柄入口；`CreateResource` 先查 `HasResource(path)`，已有条目 MUST 经 1014 拒绝（点名「路径已存在，先释放再新建」）——注册表 get_or_create 留旧弃新，门面 void 返回若静默放行，C# 队列拿到的是析构中的孤儿对象；路径查询与 `SerializeAll` 直委托。验收：构建过；2.2 直检断言全过。
- [x] 2.2 tester C++ 直检门面（不经 C#，`TestCSharpAssets` 原生段；门面方法不向外抛，断言错误通道）：坏句柄（0 / 已释放）注册 → LastErrorCode = 1001 且文本点名；未注册类型 `CreateResource` → 1014 且文本含类型名；**同路径已有条目时直接 `CreateResource` → 1014**（点名「路径已存在」，注册表留旧弃新不许静默）；空表 `HasResource` = false；`UnregisterFactory` 后 `HasFactory` = false 且旧工厂句柄 `ReleaseImpl == 1001`；`GetResourceFullPath` 与 C++ 直调逐字一致。验收：六条断言过；每条期望码与文本写死，构造对应反例时各自能红（红态读数存 `evidence/2.2-*.txt`）。

## 3. C# 资产框架（US2）

- [x] 3.1 [P] `Scripting/Managed/Assets/Texture2D.cs` 与 `Mesh.cs`：继承 `ResourceBase`，字段与 `Serialize()` 格式按 design D5（魔数 CATX/CAMH + formatVersion=1 + 小端逐字段）。网格顶点布局 MUST 与 `SceneRenderer.cs` 常量逐字相同：步长 24、位置偏移 0、颜色偏移 12、颜色 float3（验收：C# 侧三个常数与 SceneRenderer 的 VertexStride/PositionOffset/ColorOffset 逐字对照，写死期望；错一个即 4.3 像素判据红）。验收：构建过；4.2 的 C++ 读盘逐字节对照过。
- [x] 3.2 `Assets/AssetFactoryBase.cs`（Create 入队跟踪）+ `Texture2DFactory` / `MeshFactory`（GetResourceType 写死类型名；Create 里 new 对应资产类型，编译期依赖 3.1，**不并行**）。验收：4.2 的 FIFO 与阶段末队列空断言过。
- [x] 3.3 `Assets/AssetDatabase.cs`：Create（CreateResource → 出队）、Load（**先读文件 + 解析，失败抛异常且旧条目不动**；成功后才释放旧条目 → 新建 → 填数据，与导入器管线先产出后释放同构）、SaveAll（SerializeAll）；`AssetFormatException`（点名路径与原因，包装 IO 异常）。验收：4.2 往返 / 替换 / 损坏 / 缺失断言过，**损坏与缺失用例另断言旧条目仍在**（HasResource 真 + 旧字节读回一致）。
- [x] 3.4 格式解析校验：魔数 / 版本 / 长度边界，失败抛异常不静默；文件缺失 / IO 错误 MUST 包装成 `AssetFormatException`（不许裸抛 `FileNotFoundException`）。验收：版本改 2、截短一半、缺失文件三条反例各自抛**且异常类型断言为 AssetFormatException**（只断言文本会被 FileNotFoundException 喂绿）、文本点名（红态读数存 `evidence/3.4-*.txt`）；`IResource.h` 的 `CA_BIND_SKIP(Deserialize` 逐字仍在（grep 命中 1）。

## 4. 探针、用例与对外文档（US3）

- [x] 4.1 `Scripting/Managed/ResourceAssetProbe.cs`：环境变量门控（默认关 = 零行为）+ 阶段机 + 读数文件（与 `ResourceProbe` 同一套）。验收：门控关时既有用件读数零差异；阶段读数键齐全（键清单写死对照）。
- [x] 4.2 tester 第 22 用例 `TestCSharpAssets`（`Main.cpp` + `--list` 登记）：绑门面表 → 注册两工厂 → 新建填数据 → SerializeAll → **C++ 读盘逐字节对照写死期望** → 释放 + 自读加载 → 往返一致 → 替换（旧借用句柄 `ReleaseImpl == 1001`）→ 失败面（坏句柄 / 未注册类型 / 同路径直接新建 / 损坏版本 / 缺失文件 / Serialize 异常两段式：首次落盘缺席 + 二次 1012 文本；损坏与缺失另断言旧条目仍在）。验收：`--list` 输出 22 行含 TestCSharpAssets；两后端 `--headless` 该用例过；失败面六条断言各自能红，C# 侧断言异常类型 `IResourceRegistryException` + Code（坏句柄 1001 / 其余 1014）+ 文本（`evidence/4.2-*.txt`）。
- [x] 4.3 渲染消费：Texture2D → `CreateGPUTexture` → 纹理三角形；Mesh → **图内缓冲绑定**（`AllocBuffer` + `ScheduleData`，绘制 pass 按图内登记名解析顶点/索引缓冲，后端直建缓冲进不了 pass 绑定表，执行侧实测）→ 索引绘制（`AddIndexedTrianglePass`，纯增量糖层）；两后端真实像素检查（期望色写死，容差口径与纹理采样批一致）。验收：两后端像素读数 = 期望；既有正向绑定产物逐字节不变（5.1 基线覆盖）。
- [x] 4.4 对外文档：`docs/testing.md` 清单加第 22 用例**且四处「21」计数同步改 22**（表头「当前 21 个测试」、章节标题、「当前全量一共 21 条」、最小集合「两份 report 里 21 条 status」）；`CSharpHost/README.md` 编号表加 13（门面）；`Tools/CodeGen/README.md` 审计输出段「扫描 13 个接口、对账 105 条方法」改 14/114 且接口清单加门面名、错误码表加 1014 行（`verify_readme.py` 只跑 CodeGen.exe 开头的命令，抓不到审计段——本条以 grep 判据钉死，不留静默腐烂）。验收：testing.md 清单与 `--list` 输出逐条一致、四处计数 = 22（grep）；CSharpHost README 表含 13 且与锁文件一致；CodeGen README 含「14 个接口」「114 条」、门面名与 1014（grep）。

## 5. 回归与验收（US3）

- [x] 5.1 全量回归（主循环跑，子 agent 不跑全量）：两后端既有 21 用例（**去掉新增用例**）与上批基线逐条 name+status 零差异，新用例两后端独立过；CoreTests 全绿（`out\build\x64-debug\bin\CoreTests.exe` exit 0 且 `PASSED` 26 行，读数存 `evidence/5.1-CoreTests.txt`）；绑定基线**两段式**——旧锚 218 条（= 导入器批归档时的新锚，勿与那批封存的 213 旧锚混读）封存比对：既有**各接口**产物（含 codegen 产物）逐字节零差异；三份聚合产物（BindingRegistry.Bindings.h/.cpp、EngineApi.Bindings.cs）的每处差异逐处指认到门面编号 13 登记行，指认不出即红；新锚**期望 221 条** = 218 + 3 份新增（`cs_script__IResourceRegistry.Bindings.cpp` / `.Bindings.cs` / 门面登记分片 `.registry.part`，分片文件名以 build.py 标签定；结构依据：正向接口每接口两份 + 一分片，GPUBuffer 先例实测），份数或文件集不符即红（`evidence/5.1-旧锚比对.txt` / `5.1-新锚.txt`）。验收：逐条比对零差异 + 新锚 221 自洽对账 缺失 0 / 不一致 0 / 多余 0。
- [x] 5.2 验收自查（主循环可自勾）：逐条核对 spec 场景 ↔ 实测读数（registry 6 要求 / asset-layer 6 要求），红态证据齐全，`openspec validate csharp-resource-asset-foundation` valid。验收：场景对照表存 `evidence/5.2-场景对照.md`（含 CoreTests 命令与 26 行读数一行），无「声称过但无读数」的行。
- [x] 5.3 编译验证与修复（最后一项）：`python build.py --config Debug`，失败则分析修复直到成功；成功后复跑 5.1 全量确认读数不变。验收：构建输出成功标记 + 5.1 复跑读数一致（`evidence/5.3-构建.txt`）。

## 修订记录（对抗验证首次成功轮，存在面 3 条，主循环复核后修订）

1. **主张-存在-001（blocking，采纳）**：审计脚本 INTERFACES 写死 13 项，只追加审计九行必被判僵尸行红。
   - 改前（tasks 1.4 验收）：「`python Tools/CodeGen/check_bindable_audit.py` exit 0；Select-String 计数 = 9」。
   - 改后：1.4 验收改为「计数 = 9；脚本绿须与 1.5 同步」；新增 1.5（INTERFACES 加门面一项，接口数 13→14、方法 105→114、自然时序红态）；design D7 审计条、registry spec 接线要求 + 新场景「审计脚本接口表未同步时僵尸行红」、proposal 影响清单同步。依据：脚本 L27-54 INTERFACES 实测 13 项无 cs_script（主循环 read 核实）。
2. **主张-存在-002（blocking，采纳）**：「每份产物逐字节相同 / 新增只有一份」与聚合产物结构相反（上批归档实测：不一致 3 = BindingRegistry.Bindings.cpp/.h + EngineApi.Bindings.cs 合法变化）。
   - 改前（registry spec 场景）：「旧锚覆盖的每份产物 MUST 逐字节相同；新增产物只有门面那一份」。
   - 改后：两段式——各接口产物逐字节不变；三份聚合产物允许变但每处差异 MUST 指认到编号 13 登记行，指认不出即红；新锚 = 218 + 实测新增。design D8、tasks 5.1、proposal「不动」行、asset-layer spec 渲染要求同步；并写明 218 = 导入器批新锚（勿与那批 213 旧锚混读）。判据未放松（未指认差异仍红）。依据：归档 evidence/0.2-5.1-5.2-封存回归换锚.txt 实测（主循环 read 核实）。
3. **主张-存在-003（misleading，采纳）**：Mesh 颜色写 float4，真实 SceneRenderer 是 float3 偏移 12 步长 24。
   - 改前（design D5）：「位置 float3 + 颜色 float4 交错，与场景着色器输入布局一致」。
   - 改后：「位置 float3 偏移 0 + 颜色 float3 偏移 12，步长 24，与 SceneRenderer.cs 三常量逐字一致」；asset-layer spec 要求 1 写明三常数；tasks 3.1 加逐字对照验收。依据：SceneRenderer.cs L41-44 实测（主循环 read 核实）。
4. **主张-验收-001（blocking，采纳）**：5.1 残留三处——末尾「既有 codegen 产物逐字节不变」与聚合可变相矛盾；「新锚 = 218 + 实测新增」期望值自指；「report 零差异」在 22 用例下不可能。
   - 改前（tasks 5.1 末段）：「新锚 = 218 + 实测新增份数……既有 codegen 产物逐字节不变。验收：report json 读数与基线零差异」。
   - 改后：各接口产物（含 codegen）逐字节不变；新锚**写死期望 221** = 218 + 3（门面 .Bindings.cpp/.cs + 登记分片，GPUBuffer 先例实测结构）；回归口径改「既有 21 用例去掉新增逐条 name+status 零差异」（上批同口径）。registry spec 场景、design D8 同步。依据：bindings 目录实测（主循环 pwsh 核实 GPUBuffer 两份 + 各 .registry.part）。
5. **主张-验收-002（misleading，采纳）**：1.3 引用的 9999 反例是 FindRoute「未知编号」分支，缺 policy 行走 Unregistered 分支，红态无读数且被「不做变异」排除。
   - 改前（tasks 1.3）：「缺行机制沿用既有……引用 L1865 的 9999 反例，不做产品代码变异」。
   - 改后：正向读数执行者取；红态读数**主循环变异**（临时删 kBindingPolicies 门面行重建 → Unregistered 文本点名 → 恢复重建），并写明两分支文本不同。依据：攻击方证据 + 主循环复核分支语义（变异由主循环做，CLAUDE.md 允许）。
6. **主张-验收-003（blocking，采纳）**：Serialize 异常按既有机制是「下一次调用才抛 1012」，首次 SerializeAll 该资源静默不落盘——原场景要求与机制相反且 tasks 无此失败面。
   - 改前（registry spec 场景）：「错误 MUST 经 1012 上报……MUST NOT 静默跳过该资源假装成功」（首次调用即报，机制做不到）。
   - 改后：两段式场景——首次落盘缺席（文件保持写前字节、其余照写）+ 二次经代理调用抛 1012 带原始文本，两段都断言；tasks 4.2 失败面四条→五条、design D8 同步。依据：README 反向错误通道既有语义（L1157-1159）+ 空批 SubmitAndWait 首行返回（导入器批实测）；不改机制（非目标），判据未放松（两段皆可核）。
7. **主张-验收-004（misleading，采纳）**：缺文件天然抛 FileNotFoundException 即可喂绿「抛 + 点名」，AssetFormatException 类型无判据。
   - 改前（tasks 3.4 验收）：「三条反例各自抛且文本点名」。
   - 改后：加「异常类型断言为 AssetFormatException」；design D6 写明包装义务；asset-layer spec 缺失文件场景 THEN 写明类型判据。依据：spec 要求 2 本就点名 AssetFormatException，tasks/design 补齐到可核。
8. **主张-影响-002 / 主张-一致-001（blocking，同根因合并采纳）**：审计表还有顶部汇总表——脚本 summary_rows 逐接口核汇总行、合计与实算对账；只做主表九行 + INTERFACES 仍红，且 1.4 计数判据漏算汇总行。
   - 改前（tasks 1.4）：「追加九行……Select-String 计数 = 9」。
   - 改后：主表九行 + 汇总一行 + 合计 105→114；计数判据限定主表区 = 9；1.5 验收注明汇总须同步；design D7、registry spec 接线要求同步。依据：check_bindable_audit.py summary_rows（L217-247）与主循环 read 核实。
9. **主张-影响-003（blocking，采纳）**：生成产物按基名 include 门面头，CSharpHost 缺 ScriptHost 头目录，构建会红在未列改动面。
   - 改前：proposal/design/tasks 均未列 CMakeLists。
   - 改后：新增 tasks 1.6（include 目录行，C1083 自然红态）；design D7 加条目；proposal Impact 加文件；registry spec 接线清单加项。依据：CSharpHost/CMakeLists.txt L34-39 实测缺该目录（CAResource 行同类先例）。
10. **主张-影响-004（misleading，采纳）**：门面 CreateResource 返回 void + 注册表 get_or_create 留旧弃新 → 同路径直接新建时 C# 队列拿孤儿、落盘写旧字节不报错。
    - 改前：design D2 / spec / tasks 无同路径守卫。
    - 改后：门面 CreateResource 先查 HasResource，已有条目抛「路径已存在，先释放再新建」；design D2 加守卫条、registry spec 要求 3 + 新场景「同路径直接新建被显式拒绝」、tasks 2.1/2.2/4.2（失败面五→六条）同步。替换正路仍走 Load（先释放再新建）。依据：ResourceImportingSystem.cpp RegisterResource get_or_create 实测（主循环前轮 read 核实）。
11. **主张-影响-001 / 主张-一致-002（misleading，采纳）**：对外文档漏同步面（testing.md 四处 21 计数、CodeGen README 审计段 13/105，verify_readme 覆盖不到）；design Goals/Migration 与 asset-layer spec 残留全量「逐字节不变 / 零差异」断言与两段式矛盾。
    - 改前：tasks 4.4 只两文件；design Goals「既有绑定产物逐字节不变」、Migration「既有产物逐字节不变是硬判据」、asset-layer「回归 MUST 与基线零差异」。
    - 改后：4.4 扩三文件 + grep 判据（含 verify_readme 缺口说明）；Goals/Migration/asset-layer 全改两段式口径；proposal Impact 补文档文件。依据：攻击方证据 + 主循环复核（聚合产物必变，上批实测不一致 3）。
12. **主张-一致-003（minor，采纳）/ 主张-一致-004（misleading，采纳）**：1.1 按 'virtual' 数会数到 10（含虚析构）；3.2 标 [P] 但编译期依赖 3.1。
    - 改前：1.1「Select-String 'virtual' 计数 = 9」；3.2 带 [P]。
    - 改后：1.1 改数 '= 0;'（纯虚）= 9；3.2 去 [P] 并注明依赖。另采纳影响面遗留风险与一致面覆盖缺口：D3/spec 加「注册后句柄处置权归注册表，C# 不得注册期间 Release」契约；tasks 1.3/design D7 注册点须在 early-return 之前 + 门面对象生命周期随宿主；tasks 2.2 + spec 补 UnregisterFactory 独立断言（HasFactory false + ReleaseImpl 1001）。
13. **主张-溯源-001（blocking，采纳）**：Load「与导入器同构」是假的——导入器先产出后释放（ResourceImportingSystem.cpp L80-92），设计却先释放再读文件，损坏/缺失加载静默毁旧资产，spec 还把「无该路径条目」写成期望。
    - 改前（design D6 / asset-layer spec 要求 3）：「先 ReleaseResource → 读文件 → 解析 → Create」；版本损坏场景 THEN「注册表 MUST 无该路径条目」。
    - 改后：先读文件 + 解析（失败抛异常且旧条目不动），成功后才释放 + 新建 + 填数据；版本损坏 / 缺失文件场景 THEN 改「旧条目 MUST 仍在」；tasks 3.3/4.2 加旧条目仍在断言。依据：导入器管线源码顺序（主循环前轮 read 核实）；判据加严（失败不再丢资产）。
14. **主张-溯源-002（minor，采纳）**：「用户裁定」出处只有未提交的自写 ROADMAP 行。
    - 改前（proposal Why）：「用户 2026-11-23 裁定『按你建议的来』，方向已记 ROADMAP」。
    - 改后：注明原话来自本会话对话（「按你建议的来，更新Roadmap并且出一个change」），ROADMAP 是记录、随本批提交入库。依据：用户原话在本会话消息里，非凭空。
15. **主张-溯源-003（minor，采纳）**：5.1「CoreTests 全绿」无命令无份数。
    - 改前：「CoreTests 全绿」。
    - 改后：写死 `out\build\x64-debug\bin\CoreTests.exe` exit 0 + PASSED 26 行 + evidence/5.1-CoreTests.txt；5.2 对照表占一行。依据：docs/testing.md 判定行与上批 evidence 实测 26。
    - 另采纳溯源面交叉提醒（主循环核实审计表 L5 范围段 / L24 谁实现表真实存在）：tasks 1.4 补范围段 14/114 + 谁实现表一行。
16. **执行侧 blocking 冲突（apply 阶段，主循环裁定）**：spec/design 原写「门面每个方法经生成 thunk 捕获异常转错误码」——实测正向普通方法 thunk **无** try/catch（try 只在回调分支；GPUBuffer thunk_SetName 直调），照原工件实现会让 C++ 异常穿 C ABI。
    - 改前（registry spec 要求 4 / design D2 失败通道）：「MUST 经生成 thunk 捕获异常转错误码 + 文本」。
    - 改后：门面**实现自行 catch** 写生成错误通道（C# 包装自动 ThrowIfFailed 实测成立）；用码：句柄类 → 既有 1001，其余操作失败 → 新增门面专属 **1014 RESOURCE_OPERATION_REJECTED**（照 1012/1013 作用域先例只发射进门面产物，既有产物逐字节不变）；新增 tasks 1.7（CodeGenMain.cpp + check_bindings.py + README 三处同步 + 形状表「会抛异常」行改准）；2.2 断言从「抛」改「错误通道码 + 文本」；4.2 C# 侧断言异常类型 + Code；spec 加场景「1014 只发射进门面产物」；proposal/design「生成器零改动」改「仅限 1014 作用域新增」。依据：执行侧证据 + 主循环 read 核实（thunk 无 try/catch、ThrowIfFailed 自动、错误码表 L1360-1386 作用域机制）；「生成器零改动」是自设约束，按 §9 必要增量扩展直接做；判据未放松（异常不穿边界 + 既有产物逐字节不变保持）。
17. **执行侧第二阻塞（apply 阶段，主循环核实后修订）**：C2660 真根因不是旧版头——生成器字符串两段式返回分支无条件丢参（固定 `object->Method()`），任何「返回 string 且带参」的方法都生成编不过的产物；全仓仅门面这一个该形状，既有产物不受影响。执行侧已修 CodeGenMain.cpp（Len/Copy 带参 + 不支持形状生成期拒绝 + 零参数字节同构，主循环 git diff 核实 138 行）。
    - 改前：tasks 1.7 / design D7 / spec / proposal 写「唯一生成器改动 = 1014」。
    - 改后：拆 1.7（1014）+ 1.8（两段式带参，验收写死门面 Len/Copy 签名 + C2660 消失 + 既有产物逐字节不变）；design D7「两处受控改动」；spec 要求 1 与 proposal 同步。依据：执行侧证据 + 主循环 git diff 核实；判据未放松（零参数字节同构保住既有产物基线）。
18. **执行侧 apply 配套两处（主循环裁定采纳）**：① check_bindings.py「句柄泄漏」判据把 design D2 合法的 `uint64_t factoryHandle` 标量误判——豁免改为「接口头字面 uint64_t 拼写」，shared_ptr 投影句柄照旧红；新增 tasks 1.9。② 4.3/spec/proposal 原写「Mesh → CreateGPUBuffer → 索引绘制」与机制不符：绘制 pass 按图内登记名解析缓冲（执行侧核 Vulkan/D3D12 执行器 BufferHandle.FromName），后端直建缓冲进不了绑定表——改「图内缓冲绑定（AllocBuffer + ScheduleData）→ 索引绘制」；Texture2D → CreateGPUTexture 真实存在不改。依据：执行侧源码核实 + 主循环采纳；判据未放松（像素检查与产物基线保持）。