## Context

C# 调引擎必须过一层 C ABI。现有模块接口是为 **C++ 内部好用**设计的，实测（2026-10-16，对 5 个模块级接口头 + `IReadbackToken`）里混着这些过不了 C ABI 的形状：

| 方法（逐字） | 过不去的形状 |
|---|---|
| `ExecuteGraph(TaskScheduler*, castl::shared_ptr<GPUGraph> const&)` | 裸指针 + `shared_ptr` 参数 + 线程归属 |
| `CreateGPUBuffer(...)` / `CreateGPUTexture(...)` 返回 `castl::shared_ptr<...>` | `shared_ptr` 返回值 |
| `CreateShaderStruct(cacore::NameHash const&)` | native 类型（内部含 `castl::string`） |
| `Readback(ImageHandle, std::span<uint8_t>)` | **参数是最理想的形状**（`ptr + len`）；但**返回 `std::unique_ptr<IReadbackToken>`**，这个返回值同样要投影成句柄 |
| 头文件级出现频次 | `castl::vector` 33、`castl::string` 17、`cacore::NameHash` 20、`castl::shared_ptr` 10、`std::unique_ptr` 2、`castl::function` 10、`unordered_map` 1 |

方法条数口径：`CRenderBackend` 本类 **10** 个纯虚方法，同一头文件里的 `IReadbackToken` 另有 **3** 个（`Wait` / `IsReady` / `Reset`）—— 审计表**按类**分行，不按文件计。

用户当场钉死的约束：**不能为了适配 C ABI 牺牲 C++ 接口的易用性** —— 所以分工是"接口照旧写、投影由 CodeGen 承担"。那么本 change 要解决的问题就是：**把投影规则一次定死、并让"每个方法都被审过"这件事可核**。

## Goals / Non-Goals

**Goals:**

- 定死"投影矩阵"：每类 C++ 形状 → C ABI 形状 / 是否拷贝 / 谁分配 / 怎么报错。
- 把现有 6 个模块接口的每个纯虚方法审一遍，落成一张**逐方法**的表（可绑 / 需快路 / 不可绑 + 理由 + 建议）。
- 给"审计完备性"一个**会失败的装置**：接口新增方法而审计表没跟上，或"不可绑"却没理由，脚本当场红。
- 给后续两个 change（`csharp-hostfxr-bootstrap`、`csharp-bindings-generator`）一份可直接消费的输入。

**Non-Goals:**

- 不改任何接口签名、不改后端实现（易用性不让步）。
- 不写绑定生成器（第 4 个发射器）。
- 不实现 `...Span` 快路重载，只在审计表里给建议签名。
- 不动 `build.py` 的产物流程、不动 host 引导。

## Decisions

### D1 接口不动，投影归生成器（用户要求）

替代方案是"把接口改造成 C ABI 友好"（到处 `ptr+len`、返回值改句柄）—— 被否：那会把 C ABI 的负担转嫁给所有 C++ 使用方，而且 `CRenderBackend` 的签名会跨两个后端一起破。**接口保持易用，投影层是生成出来的 C++ 代码**（它自己可以自由用 `castl::vector`，所以翻译能力不受限）。

### D2 投影矩阵（本 change 的核心产物）

| C++ 接口里写的 | 投影成 | 拷贝 | 谁分配 |
|---|---|---|---|
| 定宽整数 / `bool` / `enum class` / flags | 同名定宽类型 | 无 | — |
| blittable 结构体（值或 `const&`） | `<T>C const*`（布局镜像 + 清单自检） | 无 | 调用方 |
| `castl::string const&` / 返回 `castl::string` | `char const* + len`（UTF-8，不要求 NUL） | 有 | 写入方 |
| `castl::vector<T> const&`，T blittable | `TC const* + count` | 一次 | 调用方 |
| `castl::vector<T>&`（输出） | `TC* dst + cap` → 返回所需数（两段式） | 一次 | 调用方 |
| `castl::vector<T>`，T 非 blittable | `uint8_t const* + len`（序列化块） | 一次 | 调用方 |
| `map` / `unordered_map` / `set` | 摊平成 `(K*, V*, count)` 或序列化块 | 一次 | 调用方 |
| `castl::shared_ptr<T>` **返回** | `uint64_t` 句柄（句柄表持 shared_ptr）+ 配对 `release` | 无 | 投影层 |
| `castl::shared_ptr<T>` **参数** | `uint64_t` 句柄 → 反查 | 无 | 投影层 |
| `std::unique_ptr<T>` / 其它独占智能指针（返回或参数） | 同 `shared_ptr`：一句柄 + 配对 `release`（独占语义 → 释放即作废，代际号进位） | 无 | 投影层 |
| `T const&` / `T&` 出参 | `TC const*` / `TC*` | 视 T | — |
| `castl::function<Ret(Args...)>` 回调 | 函数指针 + `void* user`（C# 侧 `[UnmanagedCallersOnly]` + 保活表） | 无 | 投影层 |
| 会抛异常的方法 | try/catch → 错误码 + 文本 | — | — |

统一规则：**元素里含 `string` / 容器 / 指针 ⇒ 一律降到序列化块**，并在生成期日志里逐条说明。

### D3 所有权只有三档，且每行必须写

① 调用方给缓冲区（首选）；② 投影层分配 + 配对释放；③ 引擎持有 + 只读视图（生命周期挂 owner 句柄）。**禁止**"native 分配、C# delete"。

`shared_ptr` 返回值的句柄表由投影层持有；句柄带代际号，释放后旧句柄**必须报"句柄失效"而不是野指针**。这条只有规则、没有实现 —— 实现留给生成器那一步（记进 Risks）。

### D4 三类不可投影：点名 + 给建议，不许静默生成

① 模板方法 / 泛型接口；② 所有权不明的裸指针；③ 依赖隐式转换的重载。这三类在审计表里标"不可绑"，理由与建议必须写。**这比"把接口改造成处处 `ptr+len`"小得多 —— 只有这三类才真需要动接口。**

### D5 热路径：加法式快路，不改易用面

需要每帧多次调用的容器参数方法，允许在接口上**另加** `...Span(T const* + count)` 重载，默认实现包装成容器调用易用版本。MUST NOT 删掉易用版本。审计表标"需快路"并给建议签名。

### D6 审计完备性用脚本兜，而不是靠自觉

`Tools/CodeGen/check_bindable_audit.py`：
- 解析被审计接口头 → 列出全部纯虚方法（`virtual … = 0;`）；
- 与 `evidence/bindable-audit.md` 的方法列对账；
- 三种非零退出：**接口里有方法但表里没有**、**表里标"不可绑"却缺理由/建议**、**表里出现接口中不存在的方法**（防抄错/防僵尸行）；
- `--list` 打印全表；退出码 `0/1/2`（2 = 找不到接口头，判据自身失效）。
- 与既有两支盘点脚本（`check_legacy_symbols.py` / `check_hash_sources.py`）同风格。

### D7 三个 change 的顺序

| 顺序 | change | 依赖 |
|---|---|---|
| 1 | **本 change**（可绑定面规则 + 审计 + 核对脚本） | 无 |
| 2 | `csharp-hostfxr-bootstrap`（把 CoreCLR 拉起来，只跑通"引擎调托管、托管回一个值"） | 无（可与 1 并行） |
| 3 | `csharp-bindings-generator`（第 4 个发射器，按本 change 的矩阵产出两侧绑定） | 需要 1 的矩阵 + 审计表；需要 2 的 host 才能验端到端 |

## Risks / Trade-offs

- **[审计表随接口演进漂移]** → 核对脚本兜住（D6 的三种非零）；脚本自身也加自检（删一行必须红）。
- **[`shared_ptr` 句柄化只写了规则、没实现 → 将来泄漏]** → 审计表每行必须写出配对释放入口；生成器那步的验收要求"分配/释放计数相等"，本 change 只保证"规则到位、每行可核"。
- **[投影拷贝带来成本]** → 热路径用 D5 的快路；审计表把"每帧多次调用"的方法标"需快路"，不靠实现者自觉。
- **[矩阵与生成器的实际实现偏离]** → 生成器那步必须复用本 change 的矩阵表做 conformance 测试（每行一个夹具），偏离即红。
- **[审计范围选错]** → 范围写死为"5 个模块级接口 + `IReadbackToken`"（`CRenderBackend` / `IWindowSystem` / `IShaderCompilerManager` / `IOManager` / `IResource` + `IReadbackToken`）。实测 `IMGUIContext/header/IMGUIContext/IMGUIContext.h` 里**没有任何纯虚方法**（它是实现类，接口在别处），所以它不作为审计对象；同一批头文件里另有的 `IWindow` / `IOBatch` / `WBatch` / `IResourceReadOnly` / `IShaderCompiler` / `UniformElement` 列入 Open Questions（按第 4 步的实际需要再扩，避免一次审太大而烂尾）。

## Migration Plan

1. 数范围 → 2. 逐方法写审计行 → 3. 写核对脚本并做红绿自检 → 4. 文档 → 5. `openspec validate` → 6. 末条：构建 + `CoreTests` + 双后端确认没碰代码。

回滚：本 change 只加两个文件 + 一节文档，回退即删。

## Open Questions

- `cacore::NameHash` 与 `castl::string` 是否统一成 `char const* + len` 一档？（倾向：是，`NameHash` 在 C ABI 面就是字符串）
- 回调（`castl::function`）的保活策略：一次性 delegate 还是 GCHandle 表？（倾向：GCHandle 表 + 显式取消注册，因为脚本可能长期持有）
- `IMGUIContext` 的接口面里若出现"每帧传整棵树"的形状，快路重载够不够、要不要改成"增量命令缓冲"？（留给第 4 步 spike 时定，本 change 只标"需快路/不可绑"）
- 同一批头文件里的 `IWindow` / `IOBatch` / `WBatch` / `IResourceReadOnly` / `IShaderCompiler` / `UniformElement` 要不要也纳入审计？（本 change 只审 5 个模块级接口 + `IReadbackToken`；扩张的判据是"第 4 步真的要从 C# 调它"）
