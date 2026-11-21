# Design: failure-visibility-hygiene

## Context

六个同族小修复（失败信号缺失），2026-11-20 全部对现状实测验真。关键事实：CoreTests 链接 CACore 静态库（`Test/CoreTests/CMakeLists.txt` `target_link_libraries(${PROJECT_NAME} PRIVATE CACore)`），FileLoader 红绿可落 CoreTests；**FileLoader 三函数全仓零活跃调用方**——`ShaderProcessor/Main.cpp` 不在构建（根 CMakeLists 的 add_subdirectory 清单无它、两份 build.ninja 零命中、磁盘无 exe），且其第 18 行调的是全仓不存在的 `fileloading_utils::LoadStringFile`（FileLoader.h 只在 `namespace cacore` 声明），属构建外死代码，本批不碰它（对抗裁决 blocking 1，2026-11-20）；`build.py` 登记层已有精确文件名先例（`for filename in ("BindingRegistry.Bindings.h", ...)` 段，注释自述前缀通配放行 `.redbak` 的实测与「红态构造只能走源码侧」）；`Tools/spin_check.py` 两个写死常量（逐字：「`EVIDENCE = ARCHIVE / "2026-09-20-engine-codegen-framework" / "evidence"`」「`TASKS = ARCHIVE / "2026-09-20-engine-codegen-framework" / "tasks.md"`」），现有 CLI 为 `queue start/end <轮次>`（轮次是位置参数，argv[3]/argv[2]）。

## Goals / Non-Goals

**Goals:**
- FileLoader 三函数失败必抛 `cacore::file_error` 点名路径（零活跃调用方，语义由 CoreTests 红绿钉死；ShaderProcessor 死代码不碰）。
- headless 跑前清 test_output 的 *.log（保留 json/png 跨运行累积产物）；capture 误用 stderr 警告（不拒绝，D4）、headless 越界 stderr 警告。
- 逐接口产物闸门精确文件名，改名件被闸门自己点名。
- spin_check 对任意 change（活跃或归档）可用。
- 两处一行文档修复 + 四条 TODO 销账。
- 全量回归绿 + 生成物逐字节不变。

**Non-Goals:**
- 孤儿产物清理（输入面删除后旧产物残留）——TODO 前缀通配条后半段，用户未点头收入本批。
- 22 处悬空 workflow 脚本的删除或路径修复——待用户裁定删/修。
- CodeGen 冷跑退出码 6（产物间依赖排序）、D3D12 假异步读回、哈希扫描盲区——各自独立批。
- 不改 FileLoader 三函数签名（抛出是行为变化，不是接口形状变化）。
- 不碰 `ShaderProcessor/`（构建外死代码：不在 CMake 构建、引用全仓不存在的 `fileloading_utils` 命名空间；其处置另案）。
- 不改 `TestReadback` 既有的 capture throw 语义。
- 不改写归档 evidence 里的历史路径串（D8）。

## Decisions

**D1 FileLoader 错误类型与依赖方向。** 新增 `cacore::file_error`（派生 `std::runtime_error`）定义在 `CACore/header/FileLoader.h`，消息形状沿用 io-error-channel 批的「<动作>：<"路径">（<原因>）」。**不复用 `ca_io::io_error`**：它定义在 `Interface/IOManager/header/IOManager/IOManager.h`，该头 include `ThreadManager.h`；CACore 是最底层静态库，让它反向依赖 IOManager/ThreadManager 不可接受。两个异常类型同形状不同命名空间，消费方各 catch 各的。

**D2 FileLoader 失败语义。** `LoadStringFile`/`LoadBinaryFile`：打不开（`is_open()` false）⇒ 抛；读取未完成（binary 按 `tellg` 尺寸读后 `gcount()` 不足；string 按 getline 循环读后流状态坏且非 eof）⇒ 抛；**空文件合法**（尺寸 0，返回空容器，不抛）。`WriteBinaryFile`：打不开 ⇒ 抛；`write()` 后流状态坏 ⇒ 抛；`flush()`/`close()` 后流状态坏 ⇒ 抛。红绿在 CoreTests 新增用例（真临时文件，`%TEMP%` 下，用例结束清理）：缺失路径读、缺失目录写——改前静默返回空/丢写（红），改后必抛且消息含路径（绿）；0 字节文件合法作对照。**读不足分支的注入限制如实登记**：`LoadBinaryFile` 的 `tellg` 与 `read` 在同一次调用内，单线程用例无法在两者之间截短文件；若找不到稳定注入手段（如并发截短线程的竞态不可靠），该分支只实现检查 + 走读证明，evidence 明写"无独立注入读数"，不编造（沿用 io-error-channel 批写流失败一支的先例）。

**D3 test_output 清理策略：跑前只清 *.log，保留 json/png，不做按后端分子目录。** headless 启动（`createDirectoryIfNeeded("test_output")` 处）遍历删除目录下既有 **`*.log` 文件**（保留目录本身、子目录、以及 `*.json`/`*.png` 等非日志产物），打印「test_output 跑前清理：N 个日志」。**不全清的依据（对抗裁决 blocking 2）**：C# 像素判据脚本（`Tools/check_csharp_*.py`，docs/testing.md 规定多次独立 headless 各写 `csharp_*_<backend>_<mode>.json` 再对账，缺一份即非零）与跨后端 PNG 对比都依赖跨运行累积产物，全清会砸掉这些判据；串味载体恰是同名日志（`*_validation.log` 等），只清日志即中病根。不选按后端分子目录的理由：大量 check 脚本与文档引用平铺路径，改目录结构波及面远超本小批。清理只发生在 headless 启动时；交互运行不清理。红绿：种一个旧时间戳哨兵 **.log** → headless 跑一次 → 哨兵必须消失（改前残留=红）；跨后端连跑 `*_validation.log` 时间戳落在第二次运行区间（绿态佐证）；**累积保留判据**：第一次 headless 写 report json + PNG，第二次 headless（不同 report 名）启动后第一次的 json/PNG 仍在、结束后两份 report 并存（改前全清方案会红——本判据钉死"只清日志"的边界）。

**D4 capture 误用=警告（不拒绝），越界警告仅限 headless。** 非 headless 带 `--capture`：参数校验阶段 stderr 警告，继续运行（沿用 TODO 原改法"打提示"）。**不选报错退出的依据（2026-11-20 实测推翻先前假设）**：`TestReadback` 的帧数是「`int frames = ctx.headlessFrames > 0 ? ctx.headlessFrames : 1;`」——交互模式也跑 1 帧，其**写图点**（`if (i == ctx.captureFrame)` → `WriteReadbackPNG`）不在 headless 门内，`--capture 0` 不带 headless 在 TestReadback 上能写出 PNG（**前提：`test_output/` 目录已存在**——非 headless 不调 `createDirectoryIfNeeded`，且 `WriteReadbackPNG` 不检查 stbi 写返回值，目录缺失时静默失败；警告文本与 evidence 如实带上这个前提）；报错退出会砍掉这条真实可用的路径。**写图点与建图点的区分（复核 0011 证伪攻击主张后钉死）**：其余五个支持 capture 的测试（TestSimpleTriangle/TestTriangleWithConstantColor/TestTriangleWithImageBuffer/TestDoublePass/TestComputeBuffer）的写图点都在 headless 门内（Main.cpp 242/449/676/897/1046 行段），门外的 `ctx.captureFrame >= 0` 分支只建回读 RT 并挂图、交互模式不产出 PNG；TestIMGUI 的 capture 读取与建 RT 全在 headless 门内。警告文本用「写出 PNG」不用「消费」，如实写「--capture 的写图仅在 headless 生效；交互模式只有 TestReadback 的单帧路径会写 PNG（依赖 test_output 目录已存在），其余测试只建回读 RT 不写图」。**验证方式如实登记**：警告在参数校验阶段发出、随后开窗，自动化验证由主循环短暂启动交互进程取 stderr 后结束进程（改前无警告=红、改后有=绿），构造方式与限制写进 evidence；不做无人值守的长时间交互运行。**帧越界警告仅限 headless**（与 spec 统一，裁决 blocking 7）：headless 运行中每个测试帧循环结束后，若 `captureFrame >= 0` 且本测试未写出 capture（帧号越界或回读无效）⇒ stderr 警告点名测试名与帧号（退出码不变）；交互运行不发逐测试警告（误用已由参数校验警告覆盖，避免帧号有效时也误报）；`TestReadback` 既有 throw 保留（全模式）。红绿：`--test TestSimpleTriangle --capture 999 --headless 1` 改前 stderr 无警告（红）、改后有（绿）；交互有效帧号不误报由走读+分支判据证明。

**D5 逐接口闸门精确文件名。** `build.py` 的「`missing = [stem for stem in expected if not any(out_dir.glob(f"{stem}.Bindings.*"))]`」一行改为按每 stem 的期望文件集精确 `is_file()` 核对。每 stem 期望哪些扩展名（`.Bindings.cs`/`.Bindings.cpp` 或仅其一）以 0.1 开工清点为准（干净构建后逐 stem 列实际产物），不凭猜测写死。**红态构造必须走源码侧（裁决 blocking 3）**：build.py 每次构建先对每个头无条件跑 `--emit-binding`、CodeGen 无条件覆写产物，随后才做缺失检查——单纯改名会在检查前被再生，闸门消息改前改后都不出现。构造：① 临时注释掉 build.py 里该头的 `--emit-binding` 调用（令本次构建不再生成该产物，沿用登记层「红态构造只能走源码侧」先例）；② 把该接口一份既有产物改名 `.redbak`；③ 跑构建——改前 glob 命中 `.redbak`、闸门消息不出现（缺陷=放行，红），改后精确核对缺失、**闸门自己的消息**（「缺少这些接口的产物」）点名该 stem（绿）；④ 还原注释与改名，全绿。判据认闸门消息文本，不认"构建失败"这个笼统结果（下游 check_bindings 也可能报错，须区分）。

**D6 spin_check 按 change 定位（保留现有位置参数，裁决 misleading 2）。** 删除 `EVIDENCE`/`TASKS` 两个写死常量。**CLI 形状钉死**：现有 `queue start <轮次>` / `queue end <轮次>` 的轮次位置参数保留不动（CLAUDE.md 与 rationale 引用的调用形状不变），新增**可选** `--change <名>` 旗标；`evidence [<evidence-dir>]` 同样加可选 `--change`。未显式指定 `--change` 时读 `openspec list --json` 取唯一活跃 change；零个或多个活跃且未显式指定 ⇒ 非零退出并说明。显式给定的名字先找 `openspec/changes/<名>/`，不存在则找 `openspec/changes/archive/*-<名>/`（恰一个匹配；多个匹配非零退出点名）；都不存在 ⇒ 非零退出。docstring 的 Usage 同步更新。红绿：① 显式 `--change io-error-channel`（已归档）跑 `queue start` → 读出 14 条全勾（未勾 0）——改前读出老 change 的 86 条（红）；② 不存在的名字 → 非零退出（改前静默读老清单，红）；③ 不带 `--change` 的旧形状 `queue start <轮次>` → 自动探测到本 change（当前唯一活跃）并读出其未勾条目（兼容判据）；④ 零活跃/多活跃分支在本 change 活跃期间无法安全构造，以走读证明分支逻辑并如实登记（不谎称实测）。

**D7 回归锚。** 开工前重取 codegen 快照（`out/build/x64-debug/codegen/` 全部 `*.codegen.h` → `%TEMP%`，保留相对结构）——**快照是独立任务 0.2，由主循环在首次构建之前执行**（裁决 misleading 1：不单独列任务就会拖到 6.2 才取，变成当前比当前恒绿）；收尾 `fc /b` 逐份比对 + `check_binding_baseline.py`（基线取归档路径 `openspec/changes/archive/2026-11-11-csharp-bindings-namespace-alignment/evidence/基线产物哈希.txt`）。本批不碰生成器与序列化运行时，产物必须逐字节不变。两后端 headless 17/17（现有清单，本批不加 tester 用例——FileLoader 红绿在 CoreTests，capture/test_output 红绿用既有用例 + 参数组合构造）。**另跑至少一条 C# 像素判据脚本全链路**（如 `Tools/check_csharp_triangle.py`，按 docs/testing.md 规定的多次 headless + 对账，主循环代跑）——证明 D3 的"只清日志"不砸跨 run 累积判据（裁决 blocking 2 的回归落点）。

## Risks / Trade-offs

- [test_output 跑前清理删掉跨运行累积产物] → D3 只清 *.log、保留 json/png（C# 像素判据多 run 对账与跨后端 PNG 对比的前提）；累积保留有独立判据钉死；回归跑一条 C# 判据脚本全链路佐证。陈旧 json（别的测试选择残留）仍可能存在——check 脚本按点名文件对账，多余文件不入场。
- [FileLoader 无活跃调用方，行为变化无运行期消费点] → 失败语义由 CoreTests 红绿钉死；ShaderProcessor 是构建外死代码（引用不存在的 fileloading_utils 命名空间），本批不碰、另案处置。
- [精确文件名闸门误伤仅产 .cs 的共享类型 stem] → 0.1 先清点每 stem 实际产物形状再写期望，不猜。
- [spin_check 自动探测在零活跃 change 时行为变化] → 显式名字路径不受影响；无活跃时要求显式指定并非零退出，比静默读错清单诚实；该分支在本 change 活跃期间无法实测，走读证明并如实登记。
- [capture 误用警告的自动化验证要短暂开交互窗口] → D4 已记验证方式（主循环短启动取 stderr 即结束进程）与限制；不放松任何既有判据（TestReadback throw 不动、交互单帧写图路径保留）。
- [交互 TestReadback 写图依赖目录已存在] → 非 headless 不创建 test_output、WriteReadbackPNG 不查 stbi 返回值；警告文本与 evidence 带上该前提，不在本批扩修（属另一条静默失败，记入 TODO 另案）。

**D8 文档修复范围（裁决 blocking 5）。** 旧基线路径串 `openspec/changes/csharp-bindings-namespace-alignment/…` 除 docs/testing.md 外，还逐字出现在 `Tools/CodeGen/check_binding_baseline.py` 的 docstring（「当前基线」与换锚对照两处）与两份**归档 evidence** 里。本批修：docs/testing.md 的基线路径 + check_binding_baseline.py docstring 的当前基线/对照路径（补 archive/2026-11-11- 前缀）；**归档 evidence 不改写**（历史记录，改写测量记录即造假）。验收判据收窄到点名文件：`git grep -F "openspec/changes/csharp-bindings-namespace-alignment/" -- docs/testing.md Tools/CodeGen/check_binding_baseline.py` 改后 0 命中（改前 3 命中，红）；全仓 grep 不作为判据（归档件永远命中）。ROADMAP 笔误修复判据不变（旧串在 ROADMAP 内 0 命中）。

## Migration Plan

1. 0.1 开工清点（调用方/产物形状/ShaderProcessor 构建外事实/spin_check 用法）；0.2 主循环取 codegen 快照（首次构建前）。
2. FileLoader（D1/D2）+ CoreTests 红绿（零活跃调用方，不碰 ShaderProcessor）。
3. tester 卫生（D3/D4）红绿。
4. build.py 闸门（D5）源码侧红绿。
5. spin_check（D6）红绿。
6. 文档修复（D8）+ 全量回归（D7，含 C# 判据脚本全链路）+ TODO 销账 + FileLoader 对外文档。

## Open Questions

（无——六项改法、错误类型、清理策略、闸门形状、spin_check 定位均已定死；孤儿产物与悬空脚本明确排除待用户另裁。）
