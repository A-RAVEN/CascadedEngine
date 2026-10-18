## 1. 先立判据（夹具与 conformance，先红后绿）

- [ ] 1.1 夹具：在 `Tools/CodeGen/BindableShapes.h` 里建夹具接口，**按 `Tools/CodeGen/README.md` 第十三节的 14 行矩阵逐行**各一个夹具方法（定宽标量与 bool/enum 合一行 / blittable 结构体 / `string const&` / 返回 `string_view` / `vector<T>`（blittable）/ 输出 `vector` / `vector<T>`（非 blittable，序列化块）/ `map`·`set` / 智能指针返回 / 智能指针参数 / `weak_ptr`（应拒绝）/ 出参 `T&` / `function` 回调 / 抛异常），并把每个方法的**期望 C ABI 签名**逐字写进 `Tools/CodeGen/BindableShapes.expected.txt`（每行前缀矩阵行号）。**怎么算做完**：输出"矩阵行数 14 / 有夹具的行数 / 无夹具的行号清单"三个读数，"无夹具的行数" MUST 为 0；每条期望签名里只出现允许的形状（容器类型名、模板尖括号、`shared_ptr`/`unique_ptr`/`weak_ptr`、C++ `&` 引用的命中数 MUST 全为 0；**不许写"某一行除外"的豁免**）；`weak_ptr` 那一行的期望写成"拒绝"（非零退出 + 类别 ②），`会抛异常` 那一行的期望写成"错误码 + 文本"。
- [ ] 1.2 写 `Tools/CodeGen/check_bindings.py`：跑生成器后**按行**比对"生成签名 == 期望签名"，并打印"矩阵行数 / 有夹具的行数 / 不符数"。**怎么算做完**：两组红态都要贴 —— **红一**：生成器还没写时跑它 → 非零退出并打印"缺产物"；**红二**（必须在产物存在之后跑）：把 `BindableShapes.expected.txt` 里某条签名故意改错一个字符 → 脚本非零退出并点名"哪一行、期望什么、实际什么"，还原后回到 0。
- [ ] 1.3 两侧对齐检查：C++ thunk 数量与名字顺序 vs C# 包装方法数量与名字顺序。**怎么算做完**：**红一**（还没有产物时）打印"两侧都缺"；**红二**（产物存在之后）：把 C# 包装里删掉一个方法名 → 检查 MUST 点名缺哪个方法、哪一侧多；还原后回到 0。

## 2. 发射器

- [ ] 2.1 标注与骨架：新增 `CA_BIND` / `CA_BIND_SKIP`（带理由）与 `--emit-binding` 开关；只解析被标注接口的纯虚方法，**先只出签名**（thunk 与包装的方法声明，不生成实现）。**怎么算做完**：对夹具接口跑一次，退出码 0，产物里方法条数 == 夹具纯虚方法数（贴两个数字）。
- [ ] 2.2 出 C++ 侧实现：thunk、`struct <接口>Api_v1 { uint32 abi_version; uint32 size; … }`、`extern "C" uint32_t <接口>_GetApi(uint32_t version, uint32_t size, <接口>Api_v1* out)`（只写 `min(调用方 size, 真实表大小)` 那一段并回填真实大小）。**怎么算做完**：`check_bindings.py` 对夹具的"不符数 = 0"；`abi_version` 与 `size` 出现在产物里（grep 命中数 ≥ 2）；用错误的版本/大小调 `GetApi` → 返回 **0** 且缓冲区未被写超（两条探针读数）。
- [ ] 2.3 出 C# 侧：包装类（方法 → `delegate* unmanaged` 字段）、blittable 句柄结构体。**怎么算做完**：1.3 的两侧对齐检查"不符数 = 0"；生成的 `.cs` 里方法条数与 C++ thunk 条数相等。
- [ ] 2.4 句柄表样板 + 配对释放：序号 + 代际号编码、`release` 函数、智能指针返回值的登记、**参数侧反查**。**怎么算做完**：用例固定落在 `Test/CoreTests/`（该目录 GLOB 自动纳入编译；不要把用例写成 `Tools/CodeGen/*.cpp`，CodeGen 的 CMakeLists 只编 `CodeGenMain.cpp`，那种文件永远不会被编译）；四条读数：释放后再用 → 返回"句柄失效"不崩溃；传有效句柄 → 反查命中并调到假实现；传已释放句柄 → 同上报失效；`CoreTests` 的 `PASSED` 行数因此 +1。
- [ ] 2.5 布局清单 + 编译期断言：每个过界结构体出 C++ / C# 两份同源常量（size + 每字段 offset），托管侧 `Boot` 逐项比对；**并且 C++ 侧同时产出 `static_assert(sizeof(T) == <n>)` 与 `static_assert(offsetof(T, <字段>) == <n>)`**（两张同源常量会一起错，静态断言把"算错布局"提前到编译期）。**怎么算做完**：把某个过界结构体一个字段从 `uint32_t` 改 `uint64_t` 且**不重新生成** → **编译失败**并点名"类型 + 字段"（贴编译器原始输出）；重新生成后编译通过；再把生成器算出的某字段 offset 手工改错一个字节 → 编译失败（两条红态 + 一条绿态）。
- [ ] 2.6 生成期拒绝按 D6 的口径：**只有 ① 模板方法与 ③ 依赖隐式转换的重载自动判、非零退出**；**② 与 ④ 一律靠带理由的 `CA_BIND_SKIP`、只打印、退出码 0**；"排除但没写理由"单独一条非零退出。**怎么算做完**：五组读数 —— ① 夹具（模板方法）→ 非零退出并点名「①」；③ 夹具（隐式转换重载）→ 非零退出并点名「③」；② 真实例子（`IWindow::GetWindowSystem`）带理由排除 → 退出码 0 且日志里有理由；同一方法**去掉理由** → 非零退出并点名"排除必须写理由"；④ 例子（`CRenderBackend::Release` 的既有理由，只作文档示例、不给该接口加 `CA_BIND`）→ 理由进日志。五组逐组贴退出码与诊断首行
- [ ] 2.7 生成前核对审计表：先给 `Tools/CodeGen/check_bindable_audit.py` 加 `--json`（字段：每个接口的逐方法结论、总数、退出码语义），生成器再以**子进程**调用 `python Tools/CodeGen/check_bindable_audit.py --json`，按退出码与 JSON 决策（**不许 import**：CodeGen 是 C++）。**怎么算做完**：脚本 `--json` 的样例输出贴出；临时删掉审计表里某条结论 → 生成器非零退出并点名"哪个方法缺审计结论"；把 `python` 从 PATH 里藏掉 → 生成器也非零退出并说明找不到 python（三组读数，后两组还原后为 0）

- [ ] 2.8 未标注的接口：对一个**没有** `CA_BIND` 的接口所在模块跑生成器。**怎么算做完**：该接口的产物文件数 MUST 为 **0**；日志"跳过"清单里 MUST 列出它，且"生成"清单里 MUST 不含它（贴两组读数）
- [ ] 2.9 句柄位数做成参数：`--handle-index-bits`（默认 **48**）与 `--handle-generation-bits`（默认 **16**），合法范围写进 `Tools/CodeGen/README.md` 的参数表（序号位 1 —（64 − 代际位数））；**不写死 32 / 56 这类无出处的界**。**怎么算做完**：README 参数表里两项都在、默认值与范围与 D5 一致（贴 README 片段）；默认产出的句柄表里序号位真的是 48（探针：代际回绕一次后旧句柄失效）；传非法组合（index=48、generation=32）→ 生成器非零退出并说明（三组读数）
- [ ] 2.10 给 `IWindow` 加标注并接入编译：在 `WindowSystem/header/CAWindow/WindowSystem.h` 的 `IWindow` 上加 `CA_BIND`，并对 `GetWindowSystem` 加 `CA_BIND_SKIP(..., "② 所有权不明裸指针：返回接口实例且无所有权说明，等接口形状改造后再绑")`；在 `build.py` 里加绑定接入清单（生成的 `IWindow.Bindings.cpp` 与 `.cs` 落 `out/build/<预设>/codegen/bindings/`），并把生成的 `.cpp` 加进 `Test/CoreTests` 的源文件（该目录 GLOB 自动纳入）；缺 .NET 时绑定这一档跳过但 MUST 打印"跳过了什么"。**怎么算做完**：生成器日志的"生成清单"逐字等于 design D1 的表（`IWindow` 一个接口）；产物的真实方法条数 = **22**（贴生成日志计数）；编译命令行里能查到 `IWindow.Bindings.cpp`（贴该行）；`python build.py --config Debug` 仍 `BUILD SUCCESSFUL`；带 `--without-managed` 时打印跳过提示（五组读数）

## 3. 文档与端到端

- [ ] 3.1 `Tools/CodeGen/README.md` 新增发射器一节：做什么 / 不做什么、`--emit-binding` 参数、`CA_BIND` / `CA_BIND_SKIP` 写法、退出码（含新增的拒绝码）、产物路径、可直接复制运行的示例与真实输出。**怎么算做完**：示例原样跑过；`python Tools/CodeGen/verify_readme.py` → `0 mismatch(es)`。
- [ ] 3.2 端到端（**必须经 `CSharpHost` 调真接口** —— 只测 C++ 侧自洽不算测过）：① 扩 `EngineApi`：末尾追加 `uint32_t (*get_interface_api)(uint32_t interface_id, uint32_t version, uint32_t size, void* out)`，`s_engineApiVersion` 从 1 升到 **2**；② `Scripting/Managed/Host.cs` 同步：新字段、`ExpectedAbiVersion = 2`，`Tick` 里用取表函数拿到 `IWindowApi_v1`，再用窗口句柄调 `GetWindowSize` 并打日志 `窗口尺寸 = W x H`；③ `Test/GPUBackendTester/Main.cpp` 把建好的那个 `IWindow` 实例登记给 `CSharpHost`（一行），并打出 C++ 侧同一时刻的 `GetWindowSize` 两个读数；④ 判据：托管侧日志的 `W x H` 与 C++ 侧那一行**逐字一致**。**怎么算做完**：三组读数 —— 绿：两边尺寸逐字一致且出现 `[托管侧] 窗口尺寸 = …`；红一：把 `Host.cs` 的 `ExpectedAbiVersion` 留在 1（旧壳配新表）→ 启动必须报 `ABI 版本不符：引擎 2`、退出码 **3**；红二：把取表函数里 `version` 传错 → 返回 0、托管侧报错、进程不崩。跑完 `--backend vulkan|d3d12` 两份 report 仍 **9/9**。
- [ ] 3.3 `openspec validate csharp-bindings-generator` → **valid**。**怎么算做完**：贴出 validate 输出。

## 4. 收尾

- [ ] 4.1 末条：`python build.py --config Debug` → `BUILD SUCCESSFUL`（托管侧产物存在）；`CoreTests.exe` → 退出码 **0**、`PASSED$` **14** 行（原 12 + 2.4 一例 + 3.2 一例）、`FAILED` **0** 行；两个后端各 `--headless 10` → 各 **9/9**（判据 = 读 `--report` 的 JSON：9 个元素、每个 `status` 为 `pass`；**不用退出码**，tester 末尾恒 `return EXIT_SUCCESS`）；`check_bindings.py` → 矩阵 14 行逐行映射、无夹具的行数 0、不符数 **0**；D9 两条脚本读数（生成物里 `Register<` 命中 0、手写文件里生成段标记命中 0）；真实方法条数 **22**；端到端（3.2）读数：托管侧 `窗口尺寸 = W x H` 与 C++ 侧逐字一致、旧壳配新表退出码 **3**。**怎么算做完**：五组读数贴出；任何一项失败修根因，不许改松判据。
