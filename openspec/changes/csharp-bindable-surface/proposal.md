## Why

C# 要能调引擎，中间必须有一层 C ABI。而**现有的模块接口是按"C++ 内部好用"设计的**，不是按"C ABI 可翻译"设计的。2026-10-16 实测（对 5 个模块级接口头 + 1 个返回类型接口）：`castl::vector` **33 处**、`castl::string` **17 处**、`cacore::NameHash` **20 处**、`castl::shared_ptr` **10 处**、`std::unique_ptr` **2 处**、`castl::function` **10 处**、`castl::unordered_map` **1 处**。这些形状过不了 C ABI —— 智能指针不能过界、容器不能过界、`NameHash` 是 native 类型、`TaskScheduler*` 的线程归属不能让脚本决定。`CRenderBackend` 本类 10 个纯虚方法里就同时有 `castl::shared_ptr<GPUBuffer>` 返回、`TaskScheduler*` 参数、`cacore::NameHash` 参数，以及 `Readback` 返回 `std::unique_ptr<IReadbackToken>`（它的**参数**形状 `std::span<uint8_t>` 反而是最理想的模板）。

关键是**不让 C++ 接口为此变丑**（这是用户当场钉死的要求：不能为了适配 C ABI 牺牲 C++ 接口的易用性）。所以正确的分工是：**接口照旧写，投影规则由 CodeGen 承担**。而 CodeGen 要能承担它，前提是**投影规则先被定死、并且逐方法可核**；否则第 4 个发射器（绑定）写出来只能覆盖几个简单方法，剩下的一碰就现做决定、两边一起返工。

本 change 只交付"规则 + 审计 + 核对装置"，**不动任何接口签名、不写生成器**。

## What Changes

- **定义一个能力 `csharp-bindable-surface`**：C++ 接口形状 → C ABI 投影形状的规则（边界只允许三类形状；容器/模板永不出现在 C ABI 面；`shared_ptr` → 句柄 + 显式释放；非 blittable 元素降级为序列化块；异常不跨边界；热路径可加零拷贝快路重载）。
- **审计 6 个接口的每个纯虚方法**：5 个模块级接口（`CRenderBackend` / `IWindowSystem` / `IShaderCompilerManager` / `IOManager` / `IResource`）+ 1 个出现在 `Readback` 返回值里的接口（`IReadbackToken`）。逐方法落一条审计行：C++ 形状 → 投影形状 → 是否拷贝 → 谁分配 → 可绑 / 需快路 / 不可绑（不可绑必须给理由与建议）。**逐接口的方法条数以核对脚本解析类体为准**（逐文件 `git grep -c` 会把同文件的其它类算进来，也会漏掉跨行的 `= 0;`）。结果写进 `evidence/bindable-audit.md`。
- **新增一个会失败的核对脚本** `Tools/CodeGen/check_bindable_audit.py`：解析被审计接口的头，列出全部纯虚方法，与审计表对账 —— **接口里任何一个纯虚方法没出现在审计表里，或"不可绑"却没写理由，脚本非零退出并点名**。这是本 change 唯一的"能红能绿"装置。
- **对外文档**：`Tools/CodeGen/README.md` 新增一节写这套规则与脚本（做什么 / 不做什么 / 参数 / 退出码 / 可直接复制的示例与真实输出）。

## Capabilities

### New Capabilities

- `csharp-bindable-surface`: C++ 接口形状到 C ABI 投影形状的规则集合，以及"每个纯虚方法都必须有投影结论"的审计完备性判据 —— 它是绑定生成器（后续 change）的输入规格，也是"接口能不能被 C# 调用"的可核答案。

### Modified Capabilities

（无）—— 本 change 不改任何既有能力的要求；绑定生成器落地时才会给 `codegen-framework` 加增量。

## Non-goals

- **不改任何接口签名**（不把 `castl::vector` 改成 `ptr+len`、不把 `shared_ptr` 返回值改成句柄）：那是把 C ABI 的负担转嫁到 C++ 使用方，正是本 change 要避免的。
- **不写第 4 个发射器**（绑定生成器）：本 change 只给它一份规则与审计输入；生成器另开 change（`csharp-bindings-generator`）。
- **不实现 `...Span` 快路重载**：只在规则与审计表里标明"哪些方法需要快路"，实现留给生成器那一步。
- **不改任何后端实现、不动 `build.py` 的产物流程**：本 change 只加规格、审计、一个只读核对脚本与文档。
- **不做 host 引导**：那是 `csharp-hostfxr-bootstrap` 的事，两者互不阻塞（见 design 的顺序表）。

## Impact

- **新增**：`openspec/specs/csharp-bindable-surface/spec.md`、`Tools/CodeGen/check_bindable_audit.py`、`openspec/changes/csharp-bindable-surface/evidence/bindable-audit.md`。
- **修改**：`Tools/CodeGen/README.md`（新增一节）。
- **不改**：任何 `.h` / `.cpp`、任何后端实现、`build.py`、任何生成物。
- **风险**：审计表会随接口演进而漂移 —— 由核对脚本兜住（接口新增方法而审计表没跟上时脚本红）；`shared_ptr` 句柄化的落地细节（谁释放、泄漏怎么防）只有规则没有实现，留给生成器那一步兑现。
