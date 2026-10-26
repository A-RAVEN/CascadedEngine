# TODO

> 单一待办清单。以后有**未完成 / 需后续处理**的事项都记在这里，按「`## [日期] 标题（所属 change/模块）`」追加，标注：状态、问题、为何、改法、验收口径、相关代码。完成即删除该条（或标记已解决）。
>
> 本文件已按「待办 / 已解决记录」两段重排（2026-09-02），**待办**在上便于一眼看清还剩什么；已解决条目保留供追溯，不再占待办视野。

---

# 待办（未解决 / 需后续处理）

## [2026-10-25] CodeGen 重编之后第一次全量跑，`VulkanCBufferBindingInfo` 会以退出码 6 失败｜构建系统（跨 change）

**状态**：待修（不阻塞：再跑一次构建就过）。

**问题**：`Tools/CodeGen/CodeGenMain.cpp` 一改，工具二进制就重编，随后 76 条反射规则**全部**重跑（日志里 `codegen: 重跑 76 条、跳过 0 条`）。这一轮里 `ShaderLibrary.h` 的解析在 `VulkanShaderFileInfo.codegen.h` 落盘之前发生，于是报
`unresolved include (fatal): VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h:245:10: fatal error: 'VulkanShaderFileInfo.codegen.h' file not found`，
工具按"编译环境不完整"拒绝出码（退出码 6），构建失败。第二次跑（`重跑 5 条、跳过 71 条`）就成功。

**为何**：增量判据只按"输入有没有变"排序，没有把"某条规则的头文件是另一条规则的产物"这条依赖写进去；平时只重跑少数几条，碰不到这个顺序。

**改法**：把产品之间的依赖登记进 `build.py` 的 codegen 规则（或让 CodeGen 在解析失败且缺的是自己产出的头时先补跑那条）。**验收**：改 `Tools/CodeGen/CodeGenMain.cpp` 的注释触发工具重编，之后一次构建就 `BUILD SUCCESSFUL`（今天要两次）。

**相关代码**：`build.py` 的 codegen 段、`Tools/CodeGen/CodeGenMain.cpp` 的 `--emit-all` 分派、日志里的 `[54/76] VulkanShaderFileInfo` / `[72/76] ShaderFileInfo` 两行。

## [2026-10-20] 绑定发射器（`--emit-binding`）不足以支撑「C# 创建窗口」：10 处桩、句柄不回收、判据恒绿｜`csharp-bindings-generator` 归档后遗留

**状态**：**形状层与判据已修**（`binding-stub-and-shapes` 已于 2026-10-25 归档）；句柄反查也已修（生成的产物里 `lookupHandle` 92 处）。**2026-10-26 复核**：下面「仍未修」清单只剩「结构体镜像只收标量」一条仍然成立，另两条已改，逐条见子项末尾的复核标注。该 change 归档时（提交 `05e483b`）声称的「端到端打通」只覆盖 `GetWindowSize` 一条（出参，走真实现路径），其余形状一次没被调到，所以下面这些缺陷当时全是绿的（这是当时的状态）。

**问题一：形状层 —— 已修，留档说明当时是什么样。** 归档时 `IWindow` 的函数表 22 个槽里有 10 个桩（桩 = 函数体只有 `return {};   // 零值…`，连 `self` 都不碰）。现在这四条已改掉：
- 所有 `bool` 返回当时都落成桩 —— 判「能直出」的条件已从 `bareType(returnSpelling) == returnType` 换成显式集合，thunk 生成 `return object->M() ? 1 : 0;`。8 个 `bool` 槽全是真实现了。
- `void*` 的返回值当时被丢掉 —— 现在投影成 `uint64_t` 不透明令牌，thunk 里 `reinterpret_cast`（只认 `void*` 这一种拼写，其它 `T*` 返回会被拒绝并要求写理由）。
- 字符串入参的 thunk 当时是空体 —— `SetWindowName` 现在从 `(char const*, uint64_t)` 造 `castl::string` 再调实现。
- 两段式当时只发声明、不发实现 —— 两个伴生各有实现体（容量 0 时一个字节都不写），且这类方法**不再进函数表、不发直出入口**，`IWindowApi_v1` 从 22 个成员变 21 个。

**仍未修（本 change 明确不做）：**
- `castl::function` 参数一律投影成固定签名 `void (*)(void*, uint32_t, float)`，与真实回调签名无关（`IWindowSystem` 的 10 个 setter 因此绑不了）。**2026-10-26 复核：已改为显式拒绝** —— `Tools/CodeGen/CodeGenMain.cpp` 给出 `rejectReason = "本轮未实现：缺回调投影路径（保活表 + 注销句柄）"`，产物里这个固定签名命中 0 处；回调形状本身仍不支持。
- 结构体镜像只收「成员全是标量」的类型，`MonitorInfo` 里的 `cacore::Rect<float>` 会被标跳过，而 `GetMonitor` 仍投影成 `MonitorInfoC` ⇒ 引用一个不存在的类型。
- 兜底那行不分 const，可写出参 `Foo*` 与入参 `Foo const&` 都出 `FooC const*`。**2026-10-26 复核：已区分** —— `Tools/CodeGen/CodeGenMain.cpp` 用 `isOutRef`（含 `&` 且不含 `const`）判出参走指针，`T const&` 走各自的形状分支。
- **夹具里三类形状仍是桩（实测 7 个，不是 6）** —— 容器 4 个（`IBindRow05::SetThings` / `IBindRow06::GetThings` / `IBindRow07::SetNested` / `IBindRow08::SetMapping`）、镜像按值返回 1 个（`IBindRow02::GetThing`）、回调 1 个（`IBindRow13::SetCallback`），外加 1 个非这套清单里的（`IBindRow01` 之外的第 7 个，需要再定位）。桩判据对夹具只打印不判定（`build.py` 每次构建会把这一行打进日志）。**这条已按 2026-10-25 用户要求搬到 `openspec/changes/binding-stub-and-shapes/tasks.md` 的 5.1，本文件不再跟踪。**
- ~~`Scripting/Managed/Host.cs:135` 手抄的表大小~~ —— **已在 `csharp-window-creation` 任务 2.6 修掉**：托管侧现在读生成物里的 `IWindowBindings.IWindowApi_v1_Size`，`TestScriptHost` 日志里「函数表大小不符」从 1 命中变 0 命中。

**问题二：句柄与生命周期（`csharp-window-creation` 已修三条，见下）。**
- **句柄入参的反查根本没实现**：`lookupHandle` 定义了但 `IBindRow09` 里零引用，注释却写着「句柄参数：反查（失效就置错误码，不崩）」。（本 change 只加了 `ResolveHandle` 这个**新入口**，"接口参数里的 `shared_ptr` / `weak_ptr` 反查"仍未实现。）**2026-10-26 复核：已实现** —— `out/build/x64-debug/codegen/bindings/*.cpp` 里 `lookupHandle` 命中 92 处。
- ~~错误状态是每接口一份 static 全局（`IWindow_lastErrorCode`），`LastErrorCode(void*)` 的 `self` 被忽略~~ —— **已在 `csharp-window-creation` 任务 5.10 修掉**：错误通道进 `IWindowApi_v2`（两个槽），状态按 `self` 分槽，每个 thunk 入口先清零；`setError` 的 `memcpy` 也改成夹住 255 字节。

**本轮新发现的三条（`csharp-window-creation` 收尾时实测到的）：**

（这三条已按 2026-10-25 用户要求搬到 `openspec/changes/csharp-window-creation/tasks.md` 的 4.1–4.3 去跟踪，本文件不再留条目 —— 它们是那个 change 范围内的未修项，`TODO.md` 只放跨 change 的账。）
- 只有 1001 / 1002 两个码，没有「未实现」这一类，桩返回的 0 与真 0 不可区分。

**问题三：判据恒绿 —— 这是根因，不是附属问题。**
- `python build.py` 与 `Tools/CodeGen/check_bindings.py` 都看不出桩：后者逐行比的是 `extern "C"` 签名文本，桩和真实现的签名**逐字相同**。
- 生成器自报「方法 22 条 / 拒绝 0 条」，把 10 条桩算成成功投影。
- 生成的 `.cs` 从没编进任何工程：`git grep "Scripting.Generated.Bindings"` 只命中 `Scripting/Managed/Host.cs` 的一句注释和发射器自己写命名空间那一行，没有任何工程或脚本引用它；常数 `8 + 8 * 22`、`56` 是手抄的。
- 夹具 14 个 `IBindRowNN` 全是合成的，没有第二个真实接口 —— `MonitorInfo` 嵌套、`string_view` 入参这些形状只有真实接口才碰得到。

**改法（三批，顺序不能换）。**
1. 先把判据改成能看出桩：生成物里数桩标记，没被列进忽略清单的桩 ⇒ 构建退出码非 0；伴生函数（`_GetWindowNameLen` / `_CopyXxx`）不再被对拍跳过，进对拍就得有函数体；生成的 `.cs` 真编进托管工程。
2. ~~补形状~~：已做（`bool` / 枚举 / `void*` / 字符串入参 / 两段式）。枚举那一条还多两步：`enum class` 不能隐式转 `int32_t`，而且**参数方向**要转回去、枚举名要补命名空间前缀（libclang 给的是非限定拼写）。
3. 句柄与创建：`keepAlive` 能回收、导出释放入口、句柄入参反查、`weak_ptr` 换成弱句柄投影，再接 `IWindowSystem` 与托管侧。

**验收口径（每条都能失败）。**
- 第 1 批：~~改之前应当是 10 处桩~~ —— 已兑现：新增的结构桩判据在改之前报 `IWindow` 桩 10 个、改完报 0 个；变异用例（把某个槽改回桩）能把它打回 8 并点名 `IWindow::WindowShouldClose`。
- 第 2 批：夹具里每个形状写一条**行为**断言（例如 `WindowShouldClose` 在窗口已关闭时返回 1、未关闭时返回 0），今天的代码会在「已关闭返回 1」这条上红。
- 第 3 批：C# 调 `NewWindow(800, 600)` 得非 0 句柄 → 用句柄 `GetWindowSize` 读回 `800 x 600` → C++ 侧 `GetWindowCount()` 由 9 变 10 → C# 调 `CloseWindow()` → 下一帧 `WindowShouldClose` 为真 → `GetWindowCount()` 回到 9。今天的代码在第一步就红（`NewWindow` 压根没进表）。

**相关代码**：`Tools/CodeGen/CodeGenMain.cpp`（`projectMethod` / thunk 生成 / `collectMirrorTypes`）、`Tools/CodeGen/check_bindings.py`、`WindowSystem/header/CAWindow/WindowSystem.h`、`WindowSystem/private/WindowSystem_Impl.cpp`、`CSharpHost/private/CSharpHost.cpp`、`Scripting/Managed/Host.cs`。

---

## [2026-09-02] IMGUI 字体 D2 加固：规范字体上传（AA 灰阶 + 去 swizzle 依赖）｜`fix-imgui-font-whitebox` deferred follow-up

**类型：变更内 deferred 跟进**（`fix-imgui-font-whitebox` 任务 3.1 / 3.2，明确"本轮不做"）

**问题**：D1（Vulkan 图像视图 honor swizzle，commit `034dcb4`）已让**白块**消失（white=97=字形，与 d3d12 一致），但链路仍是 "`GetTexDataAsAlpha8`(R 单通道) → `E_R8_UNORM` 纹理 → `SingleChannel(R)` swizzle" 的脆弱路径，且**无正常 AA 灰阶**。

**改法**（规范 ImGui 做法）：
- `IMGUIContext/IMGUIContext.cpp:489` `GetTexDataAsAlpha8` → `GetTexDataAsRGBA32`
- `:491-493` 纹理 `E_R8_UNORM` → `E_R8G8B8A8_UNORM`
- `:496` 上传大小 `texW*texH*4`
- `:754` 采样 view swizzle `SingleChannel(eR)` → **不再保留**（identity 语义；否则 RGBA 读 R=255 仍是白块）
- 仅改模块版（TestIMGUI 链接的是它）

**验收**：双端 headless 渲染出正常字形 + 出现正常 AA 灰阶；vulkan/d3d12 字节收敛；无新增 VUID。
**注**：落地时务必把 swizzle 改 identity（否则 RGBA 读 R=255 仍白）。done 后记录 AA 观察 + swizzle 一致性（`docs/TODO.md` 该 entry 移除）。

---

## [2026-07-12] vulkan-gpu-queries：Vulkan GPU 查询体系（OpenSpec change，0/13，未开始）

**状态：未开始（0/13；2026-07-12 建立后未推进，疑似搁置/废弃，待用户拍板）**

**内容**：Vulkan 完全缺 GPU 查询体系。新增 `vk::QueryPool` + `vkCmdWriteTimestamp`/`vkCmdBeginQuery`/`vkCmdEndQuery`/`vkCmdCopyQueryPoolResults`，盖 Timestamp（profiling）、Occlusion、Pipeline Statistics；暴露给引擎 profiling 层。对齐 D3D12 的 `D3D12_QUERY_HEAP`。
**决策点**：① 照做 → 从 `/opsx:apply vulkan-gpu-queries` 真开干；② 废弃 → 归档 change；③ 暂缓 → 保留现状但明确"非近期优先级"。
**相关**：`openspec/changes/vulkan-gpu-queries/`（proposal/design/tasks 齐，0 实现）。

---

## [2026-08-28] D3D12 回读改为异步，与 Vulkan 对齐（add-render-readback）

**状态：未修（技术债）**

**问题**：`add-render-readback` 的回读接口是跨后端通用的（`Readback` + `IReadbackToken`），但当前两后端实现不对称：
- **Vulkan**：异步 token —— `Readback()` 提交后返回 token，token 自持 staging+fence；`Wait()` 阻塞、`Reset()` 归还、`IsReady()` 有实义。
- **D3D12**：同步 token —— 整个 `record→execute→signal→wait→copy→释放 COM` 在 `Readback()` 内完成，token 不持 COM，`Wait()/IsReady()/Reset()` 全是 no-op。

后果：`IReadbackToken` 成了最小公分母（D3D12 上半个函数空转），两后端内部实现裂开。**目标：两后端都是真异步、对称。**

**为何当初 D3D12 做成同步**：初版 D3D12 是异步的，token 自管每次新建的 `CreateCommandAllocator/CommandList/Fence`，析构 `Release()` 时 COM 悬垂崩溃（dump：`ACCESS_VIOLATION`、`Rip=0x0` = null vtable 调用 / double-release；debug layer 另抓到 `0x87D`：分配器早于命令列表被释放）。**这是工程权衡，不是 D3D12 基础限制**——当时没根因清楚那个悬垂，又考虑低频读回同步够用，就用同步兜底了。

**正确修法（优先 A）**：
- A（推荐）：复用引擎已有 `CommandListManager`（`DirectCommand`/`ComputeCommand`）、`PooledCommandAllocator`、`GPUFrameManager`（`FrameContext::GPUWaitIdle`/`SetEventOnCompletion`），用帧上下文的命令列表/分配器做 copy+fence，与图执行共用一套，消灭"per-call 新建/裸释放"。
- B：修 per-call COM 生命周期——命令列表先于分配器释放 + 命令列表 `Reset` + 无 double-addref/release + fence 等完才释 READBACK allocation。

**验收口径**：两后端真异步对称、`Wait()` 真阻塞、无 no-op 空转；`python build.py --config Debug` 通过；vulkan+d3d12 各 8 测试 `exit 0` 全 `pass`；无 debug-layer 生命周期类报错、无 D3D12MA leaked-allocation 断言、无 crash dump、无 VUID 错误；两后端 `test_output/readback_*.png` 字节一致、`nonEmpty=true`。

**相关代码**：接口 `Interface/RenderInterface/header/CRenderBackend.h`；Vulkan（参照，异步）`VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`；D3D12（待改，同步）`D3D12RenderBackend/private/RenderBackend_D3D12.cpp`；可复用设施 `D3D12RenderBackend/private/ResourceManagment/CommandListManager.h`、`FrameBoundResourceManager.h`。

**收尾**：修好→删本条、更新 `openspec/changes/add-render-readback/design.md`（D7）与 `tasks.md`（4.3 的 [TODO] 标注）。

---

## [2026-08-30] capture 的 2 条 LOW（GPUBackendTester 截图扩展收尾）

**状态：未修（LOW，从已解决的 capture 4 问题条目中摘出的剩余项）**

**2026-10-15 复核**：LOW-2 在 `TestReadback` 这条路径上**已不再静默** —— `Test/GPUBackendTester/Main.cpp:290-293` 在"请求了 capture 却没拿到帧"时 `throw std::runtime_error("TestReadback: requested capture frame … but readback was null/blank")`；其余测试的越界帧号仍静默不写，LOW-1 也仍未修。

- **LOW-1**：`--capture` 不带 `--headless` 时**静默 noop**（无任何提示）——capture 只对 headless 生效，非 headless 下用户可能以为截了图。
- **LOW-2**：capture 帧号**越界无提示**——请求的 capture 帧号超出运行帧数时静默不写（`TestReadback` 除外，见上）。

**改法**：① 非 headless + 带 capture 时打 stderr 提示"capture 仅 headless 生效"；② capture 帧越界时 stderr 警告/拒绝。**验收**：跑测试无意外截图、越界有告警。
**相关**：`Test/GPUBackendTester/Main.cpp`（`--capture`/`--headless` 解析、`WriteReadbackPNG`）。

---

## [2026-10-15] 哈希来源盘点脚本扫不到三类位置｜`require-explicit-hash-source` 后续

**状态：未修（工具边界；今天靠编译器兜底）**

**问题**：`Tools/CodeGen/check_hash_sources.py` 按**写法**扫（`HashObj<X>` / `cacore::hash<X>` / `unordered_map<X,` / `HashDictionary<X,` 等）。**扫不到**三处：① 容器元素位置（`castl::vector<X>`）；② 手写特化体里 `hasher.hash(obj.<成员>)` 的成员类型；③ 宏包一层与别名。
**实测**：本 change 收尾时 `vk::VertexInputBindingDescription` / `vk::VertexInputAttributeDescription` / `vk::PushConstantRange` 三个类型是**编译器**报出来的，脚本同一状态报 `命中 0 个来源不明的键类型`（把 `CA_HASH_BY_BYTES(::vk::PushConstantRange)` 注释掉，脚本仍报 0）。
**改法**：把上述三类位置扫进去（至少 `castl::vector<X>` 与 `hasher.hash(obj.member)`）。
**验收口径**：注释掉 `VulkanRenderBackendNew/private/Utils/VulkanIncludes.h` 里那一行声明，脚本必须**非零退出并点名 `vk::PushConstantRange`**（今天只有编译器报红）。
**相关**：`Tools/CodeGen/check_hash_sources.py`、`Tools/CodeGen/README.md` 第十一节（边界已写明）、`openspec/changes/archive/2026-10-15-require-explicit-hash-source/`。

---

## [2026-10-15] PipelineStateDesc 那条链没有哈希来源（盘点脚本的例外清单）｜`require-explicit-hash-source` 后续

**状态：未修（今天不会静默算错：有人真用那个字典就当场编不过）**

**问题**：`PipelineStateDesc` 与它的成员链（`D3DVertexBindingSlot` / `D3DVertexBindingData` / `ShaderStateDescriptor` / `CRenderPassInfo`）既没进接入清单、也没有手写特化。它唯一的用户是 `D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.h` 末尾的 `using PipelineStatesDic = HashDictionary<PipelineStateDesc, PipelineStates>;`，而它**全仓零调用点**（死代码），所以挂在盘点脚本的例外清单上。
**改法**：补齐这条链的字段级特化（`PipelineStateDesc` 的成员含 `HashObj<…>` 与容器）。
**验收口径**：`Tools/CodeGen/hash_source_exceptions.txt` 清空、`python Tools/CodeGen/check_hash_sources.py` 仍 `命中 0`、`python build.py --config Debug` → `BUILD SUCCESSFUL`。
**相关**：`D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.h`、`Interface/RenderInterface/header/CNativeRenderPassInfo.h`、`Tools/CodeGen/hash_source_exceptions.txt`。

---

## [2026-10-15] 纹理视图与 subresource 的 4 条潜伏项（fix-imgui-font-whitebox 收尾记录）｜`fix-imgui-font-whitebox`

**状态：未修（该 change 收尾节原话：「非阻塞 latent 改进项（non-regression，列作记录，均未触达、可后续单独改）」）**

**问题**（四条，逐字摘该 change 收尾）：
1. `GetTextureView(handle, view)` 仅 External 纹理 honor swizzle；graph-local/Internal 注册 handle 仍走 identity `resource->imageView`（丢弃 swizzle）→ 将来 Internal 单通道采样不会 swizzle。
2. `VulkanTexture.cpp:240,247` 的 `baseMip` / `baseLayer` 未 clamp（只有 `mipCount`/`layerCount` 做了 0/~0u clamp）→ 潜伏 `VUID-01718/06725`。
3. identity 短路返回全范围 `m_ImageView`，忽略携带的 subresource range（与既有行为一致，未触发）。
4. 审查侧引用的 VUID 编号有差异（01762 vs 12397，format-must-match-image）——非代码缺陷。

**验收口径**：Internal 纹理传 swizzle 时采样结果与 External 一致；`baseMip/baseLayer` 越界输入被 clamp 且不再出 `VUID-01718/06725`；双端 8 测试 `exit 0`。
**相关**：`VulkanRenderBackendNew/private/VulkanObjects/VulkanTexture.cpp`、`VulkanRenderBackendNew/private/VulkanObjects/VulkanGraphLocalResourceManager.*`、`openspec/changes/fix-imgui-font-whitebox/tasks.md`（收尾节）。

---

## [2026-10-15] `Tools/spin_check.py` 把待办清单路径写死在已归档的 change 上｜工具

**状态：未修（轮界"可执行队列"装置对新 change 完全失效）**

**问题**：`Tools/spin_check.py:57` 逐字 `TASKS = ARCHIVE / "2026-09-20-engine-codegen-framework" / "tasks.md"`。实测 `python Tools/spin_check.py queue start 1` 读出来的是那份老清单的 **86 条**，与当前 change 的 `tasks.md` 无关；`queue end` 同理。
**改法**：给它加"按 change 名"的参数（或读 `openspec list` 里进行中的那个）；`evidence` 子命令与 `ARCHIVE` 的相对路径也要一起看。
**验收口径**：对某个进行中的 change 跑 `queue start`，打印的未勾条目与该 change 的 `tasks.md` 逐条一致；不存在该 change 时非零退出。
**相关**：`Tools/spin_check.py`、`openspec/changes/require-explicit-hash-source`（本次会话就是因为它读不到本 change，只能人工贴表）。

---

## [2026-10-15] 21 处 workflow 脚本引用已归档（不存在）的 change 路径｜`.claude/workflows/scripts`

**状态：未修（悬空引用；脚本一跑就读空文件）**

**问题**：14 个 `.claude/workflows/scripts/*.js` 里共 **21 处** `openspec/changes/<名字>/…` 指向已归档的 change（`engine-codegen-framework` 7 处、`harden-readback-carrier` 6 处、`fix-vulkan-dedup-key-identity` 4 处、`fix-vulkan-descriptor-registration` 2 处、`unify-depth-clamp-switch` 2 处），逐条 `Test-Path` 均为假。
**改法**：路径补上 `archive/<日期>-` 前缀（真路径可查 `openspec/changes/archive/`），或把这些一次性脚本删掉。
**验收口径**：把这些脚本里出现的每个 `openspec/changes/...` 路径逐个 `Test-Path`，全部为真。
**相关**：`.claude/workflows/scripts/`（`audit-readback-*.js`、`imgui-multiwin-*.js`、`review-codegen-marker-attribute-design.js`、`verify-codegen-tasks-order.js`、`verify-dedup-impl.js`、`unify-depth-clamp-*.js`、`tasks-adversarial-vote.js`、`vulkan-descriptor-adversarial-verify.js`）。

---

## [2026-10-15] `test_output/` 跨运行不清理，不同后端的日志串味｜`Test/GPUBackendTester`

**状态：未修（会影响"按后端读验证日志"这件事）**

**问题**：测试器只建目录不删除（`Test/GPUBackendTester/Main.cpp:1349` 只有 `createDirectoryIfNeeded("test_output")`）。实测：连跑 vulkan 与 d3d12 之后，`test_output/TestTriangleWithConstantColor_validation.log` 的时间是 **22:34:52（vulkan 写的）**，而 d3d12 自己的 `TestTriangleWithConstantColor.log` 是 22:34:54 —— 于是 d3d12 的快照里带着 vulkan 的 `VUID-VkShaderModuleCreateInfo-pCode-08740`，读日志会把上一条后端的错误算到这一条头上。
**改法**：headless 跑测前清 `test_output/`（或按后端分子目录）。
**验收口径**：连续跑完两个后端后，`test_output/` 里每个文件的时间都落在本次运行区间内；不再出现跨后端的同名残留。
**相关**：`Test/GPUBackendTester/Main.cpp`（`test_output/` 写入路径、`--report`）。

---

# 已解决记录（保留供追溯；对应条目上移至待办后此区为空可清理）

## [2026-10-15] `Experimental/DotNetHost` 原型被注释在构建外（C# 那条线的起点）｜✅已解决

**状态：已解决（2026-10-18，`csharp-hostfxr-bootstrap` 的任务 1.1 判 + 1.6 删）**

**它原来要什么**：判掉那个被 `CMakeLists.txt:91` 注释在构建外的原型（2 个文件：`private/CSharpHost.cpp` 79 行 + `CMakeLists.txt` 34 行）—— 复用还是废弃，要给依据。

**判定结论（`openspec/changes/csharp-hostfxr-bootstrap/evidence/prototype-verdict.md`）**：读到 **113 行**、覆盖 **5 项**（nethost 定位 / 三导出获取 / initialize_for_runtime_config / get_runtime_delegate / close）、缺 **7 项**。关键是它**从没跑通过"加载并调用"这半截**（`main` 只调 `load_hostfxr()`；`get_dotnet_load_assembly` 1 处定义、0 处调用），且 nethost 路径写死成 Host pack `8.0.4`（本机没这个版本）。结论 = **搬加载序列进 `CSharpHost` 模块，删目录**。

**结果**：`Experimental/DotNetHost/` 已删、根 `CMakeLists.txt` 第 91 行的注释已删；加载序列搬进 `CSharpHost/private/CSharpHost.cpp`（nethost 定位改用 CMake glob 取已装 Host pack，实测取到 10.0.2）。同 change 的 1.5 已跑通端到端：`[托管侧] 托管侧已启动：ABI 版本 1、函数表 24 字节` + `加载耗时 = 44 ms` + `Tick #1` 回值一致。

---

## [2026-08-28] 压缩/复杂格式读回载体 & bpp 设计原则（add-render-readback 扩展边界）｜✅已解决

**状态：已解决（2026-10-10，`harden-readback-carrier` 落地并归档 → `openspec/changes/archive/2026-10-10-harden-readback-carrier/`；归档提交 `fb41fa4`）**

**它原来要什么**：三条原则（压缩格式读回是**块线性**载体、block 参数与分辨率正交、公开面只有单一尺寸查询 `GetImageByteSize` 而不暴露每像素 bpp）+ 一个"待修"块（私有的 `GetReadbackBpp` / `D3D12ReadbackBpp` 与 `GetFormatBlockSize` 重复且已分叉、`E_D32_SFLOAT_S8_UINT` 8 vs 4、未知格式静默 fallback=4）。

**结果（2026-10-15 复核）**：`GetImageByteSize` 成了唯一公开来源 —— 今天 `GetImageByteSize` 10 处、`GetReadbackBpp` 与 `D3D12ReadbackBpp` **各 0 处**；未支持格式（压缩 / 深度-模板 multi-aspect / 无紧凑布局）**显式拒绝**并返回 null token（`D3D12RenderBackend/private/RenderBackend_D3D12.cpp:614`、`:620` 的报错），不再静默回 4；三条原则随该 change 写进主 spec `openspec/specs/resource-readback/spec.md`。

---

## [2026-08-28] review workflow 确认的另两条读回缺陷｜✅已解决

**状态：已解决（同一次 `harden-readback-carrier`，2026-10-10；归档提交 `fb41fa4`）**

- **缺陷 A（载体硬编码 mip0/layer0、MSAA 未 resolve）**：走的是原「修法 ①」——支持范围收窄为「单 mip、单 layer、非 MSAA、`ETextureType::e2D`」，其余**显式拒绝**：`D3D12RenderBackend/private/RenderBackend_D3D12.cpp:638` 报错原文以 `Readback(image) only supports single-mip/single-layer/non-MSAA/2D` 开头；接口注释同步（`Interface/RenderInterface/header/CRenderBackend.h:83-85`）。
- **缺陷 B（token 持裸 span → dst UAF 隐患）**：走的是原「修法 ①」——契约写死在接口注释（`CRenderBackend.h:44-48` 逐字含 `Caller owns dst and MUST keep the memory it refers to alive AND its size unchanged until Wait() returns`），后端在写入前**防御性校验 `dst.size()`**（`RenderBackend_D3D12.cpp:629` 与 `:761` 的 `dst too small` 报错）。
- **未选「修法 ②」**（token 自己拷一份）：多一次 memcpy 不值，契约 + 校验已能挡住越界写。

---

## [2026-09-01] 观测 Vulkan IMGUI 多窗口副窗偏移——逐窗口回读 RT（新增捕获能力）｜✅已解决

**状态：已解决（2026-09-02，`fix-vulkan-dedup-key-identity` 修复并归档 → `openspec/changes/archive/2026-09-02-fix-vulkan-dedup-key-identity/`，commit `c3cbc49`）**

> 本条记录"观测能力 + 根因定位"全过程（含多轮证伪）。根因已被上述 change 以内容键修复，下列细节保留供追溯，非新待办。

**结果**：
- **观测能力**：逐窗口回读已落地（每窗同尺寸 readable RT 镜像回读 + `--capture`，输出 `test_output/TestIMGUI_<backend>_viewport<N>.png`），副窗偏移首次被 PIL 量化（`(61,61)`/`(349,341)` = 副窗pos−主窗pos）。headless 不再是盲区。
- **根因（Vulkan 后端）**：`VulkanShaderResourceSet::Init` 去重 hash 忽略 per-viewport `VulkanShaderStruct*`；所有 ImGui viewport 共享 shader path（"Shaders/Imgui"）+ 绑定名（imguiCommon/imguiTextureBinding）→ hash 合并到 **main** 实例 → 绑定 main 的 `IMGUIScale_Pos` → 副窗内容偏移 `(副窗pos−主窗pos)`。
- **修法**：改为**内容键**（`operator==` 比较 path + resourceDic 的 [name→structPtr]；hash 加入 structPtr；`m_ShaderResourceInstances` 以完整 `VulkanShaderResourceSet` 为 key）。修复后副窗 v1/v2 vulkan≡d3d12 字节一致（meanDiff 0.000），偏移消除、测试回归通过。
- **遗留**：交互式关窗时 VUID-09600/01282（本 change 触碰面外，疑似交互式布局/teardown），未在本 change 触达，另案（见「vulkan 显示异常」历史）——如需定论做 baseline 验证。
- 多轮证伪细节（Round-2 等）见归档 change 的 design/tasks.md。

---

## [2026-08-31] Vulkan IMGUI 多窗口副窗偏移（GUI 未与窗口对齐，D3D12 正常）｜✅已解决

**状态：已解决（2026-09-02，与上方 [2026-09-01] 同根同源——均被 `fix-vulkan-dedup-key-identity` 修复**）

> 此前"真因未定位（静态分析穷尽）"，被 [2026-09-01] 的运行插桩推翻——真因 = Vulkan 后端常量绑定去重错误（见上条）。修复后副窗内容与 d3d12 字节一致，偏移消除。
> 已证伪项（视图 Pos 过度偏移/负高 viewport/共享 framebuffer/字体修复相关/逻辑-物理尺寸分裂 等）见归档 change 的 design.md；为避免 TODO 冗余，本条收敛到"已解决"。

---

## [2026-08-30] IMGUI 字体渲染成白块（文字不可读）｜✅已解决（D1）

**状态：已解决（D1，`fix-imgui-font-whitebox`，未归档；D2 见待办 [2026-09-02]）**

**结果**：根因是 Vulkan 采样图像视图丢失接口 `GPUTextureSwizzle::SingleChannel(eR)`（未翻译成 `vk::ComponentMapping`）→ R8 视图按 identity 采样得 `(R,0,0,1)`，`Imgui.slang:14` `.aaaa` 读到 A=1 → 覆盖率恒 1 → 白块。**修正**：此前"双端均复现"是缩略图目测误判——D3D12 的细字形被当成白块，D3D12 实为正常（`Shader4ComponentMapping` 已正确）。
- **D1（已修，commit `034dcb4`）**：Vulkan 创建采样视图应用 swizzle（`viewInfo.components`），`SingleChannel` 在 `.aaaa` 读到 R 覆盖率；`VulkanGraphLocalResourceManager::GetTextureView` 增重载接收 view swizzle；绑定处传入。`TestIMGUI_vulkan.png` white=97（=d3d12，非原 233 实心块），渲染出 "Options" 字形，无新增 VUID。
- **D2（未做 → 待办）**：规范字体上传 `GetTexDataAsRGBA32` + `E_R8G8B8A8_UNORM` + identity swizzle（得到 AA 灰阶），见待办 [2026-09-02]。

---

## [2026-08-30] 暴露 enableDepthClamp（z-clip/z-clamp 开关）为 GPUGraph 级开关，统一双后端光栅化

**状态：已解决（2026-08-30，`unify-depth-clamp-switch` 落地并归档 → `openspec/changes/archive/2026-08-30-unify-depth-clamp-switch/`）**

**结果**：统一 `RasterizerStates::enableDepthClamp`（默认 false = 标准近平面裁剪），双后端一致实现——Vulkan 映射 `depthClampEnable` + 设备启用 `depthClamp` feature；D3D12 去硬编码、`DepthClipEnable = !enableDepthClamp`（双路径一致）；ConstantColor/StructuredBufferColor 顶点 z 改 ≥0。`python build.py --config Debug` 通过，双后端 8 测试 exit 0，ConstantColor/StructuredBufferColor 双端渲染出三角形且字节一致，无新增 VUID。对抗验证 11/11 全票（mustFix 空）。主 spec `openspec/specs/pipeline-depth-clamp/spec.md` 已同步（valid）。**遗留**：[AUDIT-1] 覆盖缺口——clamp 正向路径（enableDepthClamp=true）未被任何测试运行时触达，仅代码审+MCP 语义核实，可补一条真 clamp 渲染测试（非必须）。

---

## [2026-08-29] 截图功能的 4 个 capture 代码问题（GPUBackendTester 截图扩展）

**状态：已解决（2026-08-29，A 修复落地）**

**结果**：① `WriteReadbackPNG` 在 null/空白时 `return false` 并打 stderr（vulkan ConstantColor 实测输出 `[capture] ... capture is BLANK (nonEmpty=false)`）；② TestReadback 在"请求了 capture 但读到 null/空白"时 `throw`（兑现 assert，读回验证失败即 fail）；③ TestIMGUI 赋值行补 `captureRT.IsValid()`；④ IMGUIContext::DrawSingleView 用 `ImGuiViewportFlags_CanHostOtherWindows` 门控到主 viewport（此 imgui 版本无 IsMainWindow）。重编译通过，双后端 8 测试 exit 0、4 对字节一致不变。**剩余 2 条 LOW 已摘出 → 待办 [2026-08-30] capture 2 条 LOW。**

---

## [2026-08-30] vulkan 08114/00331 + D32 depth usage —— 已由 fix-vulkan-descriptor-registration 落地解决

**状态：已解决（2026-08-30，`fix-vulkan-descriptor-registration` 落地并归档）。**

- **08114/00331**：D-A（RAST shader 绑定→RWState）+ D-B（MarkResourceUse 生命周期）+ D-C（持久 offset 绑所有 + per-pool 峰值）+ D1补全（顶层 Foreach insert-only 防 orphan）+ D2（BuildResources 兜底 inert）+ D3（usage 位）落地后，StructuredBufferColor/DoublePass/ComputeBuffer **08114/00331 = 0**，截图真实非黑；vulkan/d3d12 三对捕获字节一致；IMGUI 08114=0 属独立（外部字体图）。
- **D32 depth 02251 / 01209 / 01758 / 02633 / 08931**：D-C bind-all 暴露后，已修 `CollectResources` depth attachment 用 `eDepthStencil`（非 `eRT`），ConstantColor 现仅剩 2× shader-code 08740（另案）。
- **附带 barrier 缺陷（raster 不登记采样读 → 跨 pass 写→读未串行化）**：D-A 已让 raster shader 采样进 RWState → 写→读入生命周期/屏障链，已被覆盖。

**审查闭环**：3 轮对抗验证（4 审查者 / 轮，逐 claim 投票）——第二轮修正 C10（跨别名内存依赖改诚实 submission-local 记录 + 移除不成立 compute 分支）、C11（IMGUI 归因更正）；第三轮收敛（mustFix 清空，bottomLine "TRUSTWORTHY and behaviorally COMPLETE"）。**诚实声明**：C10 跨别名内存依赖为 submission-local best-effort——真正跨批次内存依赖需提交模型改动（单 submit 全批次 / 批间 semaphore），超范围未 overclaim；跨批次正确性依赖既有 waitForFences 串行化 + 设备一致性。

**归档**：`openspec/changes/archive/2026-08-30-fix-vulkan-descriptor-registration/`；delta spec 已同步主 spec。
