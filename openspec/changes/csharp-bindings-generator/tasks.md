## 1. 先立判据（夹具与 conformance，先红后绿）

- [ ] 1.1 夹具：在 `Tools/CodeGen/BindableShapes.h` 里建夹具接口，**按 `Tools/CodeGen/README.md` 第十三节的 14 行矩阵逐行**各一个夹具方法（定宽标量与 bool/enum 合一行 / blittable 结构体 / `string const&` / 返回 `string_view` / `vector<T>`（blittable）/ 输出 `vector` / `vector<T>`（非 blittable，序列化块）/ `map`·`set` / 智能指针返回 / 智能指针参数 / `weak_ptr`（应拒绝）/ 出参 `T&` / `function` 回调 / 抛异常），并把每个方法的**期望 C ABI 签名**逐字写进 `Tools/CodeGen/BindableShapes.expected.txt`（每行前缀矩阵行号）。**怎么算做完**：输出"矩阵行数 14 / 有夹具的行数 / 无夹具的行号清单"三个读数，"无夹具的行数" MUST 为 0；每条期望签名里只有三类允许形状（脚本可数：容器类型名命中数 = 0，序列化块那一行除外）。
- [ ] 1.2 写 `Tools/CodeGen/check_bindings.py`：跑生成器后**按行**比对"生成签名 == 期望签名"，并打印"矩阵行数 / 有夹具的行数 / 不符数"。**怎么算做完**：两组红态都要贴 —— **红一**：生成器还没写时跑它 → 非零退出并打印"缺产物"；**红二**（必须在产物存在之后跑）：把 `BindableShapes.expected.txt` 里某条签名故意改错一个字符 → 脚本非零退出并点名"哪一行、期望什么、实际什么"，还原后回到 0。
- [ ] 1.3 两侧对齐检查：C++ thunk 数量与名字顺序 vs C# 包装方法数量与名字顺序。**怎么算做完**：**红一**（还没有产物时）打印"两侧都缺"；**红二**（产物存在之后）：把 C# 包装里删掉一个方法名 → 检查 MUST 点名缺哪个方法、哪一侧多；还原后回到 0。

## 2. 发射器

- [ ] 2.1 标注与骨架：新增 `CA_BIND` / `CA_BIND_SKIP`（带理由）与 `--emit-binding` 开关；只解析被标注接口的纯虚方法，**先只出签名**（thunk 与包装的方法声明，不生成实现）。**怎么算做完**：对夹具接口跑一次，退出码 0，产物里方法条数 == 夹具纯虚方法数（贴两个数字）。
- [ ] 2.2 出 C++ 侧实现：thunk、`struct <接口>Api_v1 { uint32 abi_version; uint32 size; … }`、`extern "C" uint32_t <接口>_GetApi(<接口>Api_v1* out)`。**怎么算做完**：`check_bindings.py` 对夹具的"不符数 = 0"（绿态读数）；`abi_version` 与 `size` 出现在产物里（grep 命中数 ≥ 2）。
- [ ] 2.3 出 C# 侧：包装类（方法 → `delegate* unmanaged` 字段）、blittable 句柄结构体。**怎么算做完**：1.3 的两侧对齐检查"不符数 = 0"；生成的 `.cs` 里方法条数与 C++ thunk 条数相等。
- [ ] 2.4 句柄表样板 + 配对释放：序号 + 代际号编码、`release` 函数、智能指针返回值的登记。**怎么算做完**：写一个最小 C++ 单测（`Tools/CodeGen/TestBindHandle.cpp` 或 CoreTests 里加一例）验证"释放后再用 → 返回句柄失效错误码、不崩溃"；贴出该单测的通过读数。
- [ ] 2.5 布局清单 + 启动自检：每个过界结构体出 C++ / C# 两份同源常量（size + 每字段 offset），托管侧 `Boot` 逐项比对。**怎么算做完**：故意把某结构体一个字段从 `uint32_t` 改 `uint64_t` 且不重新生成 → 自检失败并点名"类型 + 字段"（贴原始输出）；恢复后通过。
- [ ] 2.6 四类不可投影的拒绝：① 模板方法 ② 所有权不明裸指针 ③ 依赖隐式转换的重载 ④ 排除未写理由 —— 各造一个夹具，生成器必须**非零退出**并点名"接口::方法 + 类别 + 建议"；带理由的 `CA_BIND_SKIP`（用 `CRenderBackend::Release` 的真实理由）只打印不报错。**怎么算做完**：四组红 + 一组绿，共五组读数，逐组贴出退出码与诊断首行。
- [ ] 2.7 生成前核对审计表：import `check_bindable_audit.py` 的解析逻辑（不复制代码），确认被标注接口每个纯虚方法都有审计结论。**怎么算做完**：临时删掉审计表里某条结论 → 生成器非零退出并点名"哪个方法缺审计结论"（贴读数）；恢复后 0。

- [ ] 2.8 未标注的接口：对一个**没有** `CA_BIND` 的接口所在模块跑生成器。**怎么算做完**：该接口的产物文件数 MUST 为 **0**；日志"跳过"清单里 MUST 列出它，且"生成"清单里 MUST 不含它（贴两组读数）。
- [ ] 2.9 函数表只追加：让同一夹具产出 v2 表（只在末尾多一个字段）。**怎么算做完**：用 v1 的期望 `size` 校验 v2 表 → **通过**，且日志把新增字段标成"不可用"；用 v2 的期望 `size` 校验 v1 表 → **拒绝**并说明哪一版太小（四组读数：两方向各红/绿）。

## 3. 文档与端到端

- [ ] 3.1 `Tools/CodeGen/README.md` 新增发射器一节：做什么 / 不做什么、`--emit-binding` 参数、`CA_BIND` / `CA_BIND_SKIP` 写法、退出码（含新增的拒绝码）、产物路径、可直接复制运行的示例与真实输出。**怎么算做完**：示例原样跑过；`python Tools/CodeGen/verify_readme.py` → `0 mismatch(es)`。
- [ ] 3.2 端到端一条：在 `Scripting.Managed` 的托管壳里调 `IWindow::GetWindowSize` 拿回真实窗口尺寸并打日志；同时在 `Test/GPUBackendTester` 的 `TestSimpleTriangle` 里加一行 `std::puts` 打出同一时刻 C++ 侧的 `GetWindowSize` 两个读数（这一行写进 Impact）。**怎么算做完**：托管侧日志里出现"窗口尺寸 = W x H"，且与 `TestSimpleTriangle` 打出的两个读数**逐字一致**（两处读数贴出）。若 `csharp-hostfxr-bootstrap` 还没做完，本条记成"被依赖挡住"并附该 change 的 `remaining` 数，不许删条目。
- [ ] 3.3 `openspec validate csharp-bindings-generator` → **valid**。**怎么算做完**：贴出 validate 输出。

## 4. 收尾

- [ ] 4.1 末条：`python build.py --config Debug` → `BUILD SUCCESSFUL`（托管侧产物存在）；`CoreTests.exe` → 退出码 **0**、`PASSED$` **12** 行、`FAILED` **0** 行；两个后端各 `--headless 10` → 各 **8/8 pass**；`python Tools/CodeGen/check_bindings.py` → 不符数 **0**；`python Tools/CodeGen/check_bindable_audit.py` → 命中 **0** 处。**怎么算做完**：五组读数贴出；任何一项失败修根因，不许改松判据。
