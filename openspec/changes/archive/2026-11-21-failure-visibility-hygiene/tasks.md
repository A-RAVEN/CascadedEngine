# Tasks: failure-visibility-hygiene

## 0. 开工清点

- [x] 0.1 四项清点（命令+输出原文存档 `evidence/0.1-清点.txt`）：① FileLoader 三函数全仓调用点（`git grep -n "LoadStringFile\|LoadBinaryFile\|WriteBinaryFile"`）——预期零活跃调用方：ShaderProcessor/Main.cpp 的调用写的是 `fileloading_utils::LoadStringFile`（全仓无该命名空间），须核根 CMakeLists add_subdirectory 清单无 ShaderProcessor、build.ninja 零命中、无 exe，把"构建外死代码"事实存档；② `build.py` `CODEGEN_BINDINGS` 每个期望 stem 的实际产物形状（干净构建后逐 stem 列 `.Bindings.cs`/`.Bindings.cpp` 存在性，形成精确期望表）；③ spin_check.py 现有子命令、位置参数结构（argv 下标）与写死常量逐字引文；④ build.py 绑定生成段无条件重写产物的事实（`--emit-binding` 调用点与闸门检查的先后），为 3.1 源码侧红态构造定位。验收：四项输出齐备；出现清单外 FileLoader 活跃调用方即修订 proposal Impact 与 design 并记录。
- [x] 0.2 codegen 快照（主循环执行，首次构建之前）：`out/build/x64-debug/codegen/` 全部 `*.codegen.h` 拷贝到 `%TEMP%`（保留相对目录结构），记录份数。验收：快照目录份数与源目录一致（命令输出存档）；6.2 的 fc /b 以此为锚。

## 1. FileLoader 失败显式化（design D1/D2）

- [x] 1.1 `CACore/header/FileLoader.h`：新增 `cacore::file_error`（派生 `std::runtime_error`，构造收动作+路径+原因，消息形状「<动作>：<"路径">（<原因>）」）；三函数声明不变。验收：与 1.2 同批落地后统一构建（本条不单独验收构建）。
- [x] 1.2 `CACore/private/FileLoader.cpp`：三函数按 D2 补失败（打不开/读不足/写失败/flush 失败抛 `file_error` 点名路径；空文件合法）。红绿在 CoreTests 新增 FileLoader 用例（真临时文件，%TEMP% 下，用例结束清理）：①缺失路径读必抛且消息含路径——改前红（静默返回空）；②0 字节文件读返回空不抛——改前后均绿（对照）；③二进制读不足分支实现 gcount 检查——tellg 与 read 同调用内、单线程无法介入截短（D2 已记），无稳定注入手段时只实现检查+走读证明，evidence 明写"无独立注入读数"，不编造；④缺失目录写必抛——改前红（静默丢写）。红态读数必须先于实现修复存档。
- [x] 1.3 零活跃调用方事实存档（不碰 ShaderProcessor）：把 0.1① 的证据（git grep 输出、根 CMakeLists 清单、build.ninja 零命中、fileloading_utils 无声明）整理进 `evidence/1.3-零调用方.txt`，并在 FileLoader 对外文档（6.1）写明"当前无活跃调用方，失败语义由 CoreTests 钉死"。验收可失败：`Select-String -Path proposal.md,design.md,specs\*\*.md -Pattern '唯一调用' -Encoding UTF8`（在本 change 目录下）0 命中——改前 proposal/spec 有该断言（红）；tasks 修订记录的改前引文与本条自引不计；evidence 含四项命令真实输出。ShaderProcessor 死代码处置另案，不在本批。

## 2. tester 输出卫生（design D3/D4）

- [x] 2.1 `Test/GPUBackendTester/Main.cpp`：headless 启动处（`createDirectoryIfNeeded("test_output")`）删除目录下既有 **`*.log` 文件**（保留目录、子目录、json/png 等非日志产物），打印「test_output 跑前清理：N 个日志」。红绿（主循环代跑）：①种旧时间戳哨兵 **.log** → headless 跑一次 → 哨兵消失且计数打印——改前红（哨兵残留）；②跨后端连跑后 `*_validation.log` 时间戳落在第二次运行区间（绿态佐证）；③**累积保留判据**：第一次 headless 写 `--report` json + 截图 PNG，第二次 headless（不同 report 名）启动后第一次的 json/PNG 仍在、结束后两份 report 并存——全清方案在此必红，钉死"只清日志"边界（D3）。
- [x] 2.2 参数校验：非 headless 带 `--capture` ⇒ stderr 警告「--capture 的写图仅在 headless 生效；交互模式只有 TestReadback 的单帧路径会写 PNG（依赖 test_output 目录已存在），其余测试只建回读 RT 不写图」，继续运行（不拒绝，D4 实测依据与前提）；`--help` 文本同步。红绿：由主循环短暂启动交互进程取 stderr 后结束进程——改前无警告（红）、改后有（绿）；构造方式与限制如实写进 evidence。写图点/建图点区分已由复核钉死（五测试写图点在 headless 门内、TestIMGUI 全在门内），不再重复清点。
- [x] 2.3 capture 未写出警告（**仅限 headless**，与 spec 统一）：headless 运行中每个测试帧循环结束后，`captureFrame >= 0` 且本测试未写出 capture（帧号越界或回读无效）⇒ stderr 警告点名测试名与帧号（退出码不变）；交互运行不发逐测试警告（误用由 2.2 参数校验警告覆盖，避免帧号有效时误报）；`TestReadback` 既有 throw 一字不动（全模式）。红绿（主循环代跑）：`--test TestSimpleTriangle --capture 999 --headless 1` 改前 stderr 无警告（红）、改后有（绿）；`--capture 0 --headless 1` 正常写出 PNG 不受影响（对照）；交互有效帧号不误报由分支走读证明（D4）。

## 3. 绑定产物闸门（design D5）

- [x] 3.1 `build.py` 逐接口期望检查（「`missing = [stem for stem in expected if not any(out_dir.glob(f"{stem}.Bindings.*"))]`」一行）：前缀通配改按 0.1② 精确期望表逐文件 `is_file()` 核对，缺失消息沿用「缺少这些接口的产物」点名 stem。红绿**走源码侧**（主循环代跑构建；生成段无条件重写产物，单纯改名会被再生，D5/裁决 blocking 3）：①临时注释掉该头的 `--emit-binding` 调用 + ②把该接口一份既有产物改名 `.redbak` + ③跑构建——改前 glob 命中改名件、闸门消息不出现（红）；改后闸门消息点名该 stem（绿）；④还原注释与改名后全绿。判据认闸门消息文本，不认笼统"构建失败"（与下游 check_bindings 报错区分）。

## 4. spin_check 按 change 定位（design D6）

- [x] 4.1 `Tools/spin_check.py`：删 `EVIDENCE`/`TASKS` 写死常量；**保留现有轮次位置参数**（`queue start <轮次>` / `queue end <轮次>` 形状不变，CLAUDE.md/rationale 引用的调用方式不受影响），新增可选 `--change <名>` 旗标（`evidence` 同）；未指定时取唯一活跃 change；显式名字先找活跃目录再回退归档（恰一个匹配）；零个/多个活跃未显式指定、名字不存在、归档多匹配 ⇒ 非零退出点名；docstring Usage 同步。红绿：①显式 `--change io-error-channel`（已归档）跑 `queue start <轮次>` 读出 14 条全勾（未勾 0）——改前红（读出老 change 86 条）；②不存在的名字非零退出——改前红（静默读老清单）；③不带 `--change` 的旧形状自动探测到本 change（当前唯一活跃）并读出其未勾条目（兼容判据）；④零活跃/多活跃分支在本 change 活跃期间无法安全构造——走读证明分支逻辑并如实登记，不谎称实测。

## 5. 文档修复与销账

- [x] 5.1 文档路径修复（D8 范围）：①`docs/testing.md` 基线路径与 ②`Tools/CodeGen/check_binding_baseline.py` docstring 的当前基线/换锚对照路径，`openspec/changes/csharp-bindings-namespace-alignment/…` → `openspec/changes/archive/2026-11-11-csharp-bindings-namespace-alignment/…`（仅 docstring 文字，脚本逻辑不动）；③`docs/ROADMAP.md`「也不要要求向游戏脚本开放原生线程调度」→「也不要求向游戏脚本开放原生线程调度」。**归档 evidence 里的历史路径串不改写**。验收可失败：`git grep -F "openspec/changes/csharp-bindings-namespace-alignment/" -- docs/testing.md Tools/CodeGen/check_binding_baseline.py` 改后 0 命中（改前 3 命中，红）；ROADMAP 旧串在该文件内 0 命中。全仓 grep 不作判据（归档件永远命中）；"新路径 Test-Path 为真"恒真、不作判据。
- [x] 5.2 docs/TODO.md 销账（修好即删，按该文件规则）：capture 2 LOW 条、spin_check 条、test_output 条**三条整条删除**；前缀通配条**只删本批修掉的前半段，孤儿产物后半段保留为独立条目并改写注明**（用户未点头收入本批）。验收：三条被删标题 `Select-String` 0 命中；孤儿产物保留条存在且注明来源；前缀通配原标题不再出现。

## 6. 对外文档与全量回归

- [x] 6.1 FileLoader 对外文档（与代码同目录）：CACore 下新增或补 README 节，写三函数错误行为表（打不开/读不足/写失败/flush 失败/空文件合法五行，各配改前静默、改后 file_error 对照）+ 可复制示例与真实输出（照抄 1.2 evidence 读数，不得手编）。验收：文件存在、五行齐备、示例输出与 evidence 一致。
- [x] 6.2 全量回归（design D7）：以 0.2 快照为锚（首次构建前已取）；`python build.py --config Debug` 通过；CoreTests 全绿（含 FileLoader 新用例）；两后端 headless 17/17；**至少一条 C# 像素判据脚本全链路通过**（如 `Tools/check_csharp_triangle.py`，按 docs/testing.md 规定的多次 headless + 对账，主循环代跑——证 D3 只清日志不砸跨 run 累积）；`check_binding_baseline.py`（归档基线路径）0/0/0；`fc /b` 对 0.2 快照逐份全同。验收：各项输出存档 evidence；任一差异即红。

## 修订记录

（执行期间对 proposal/design/specs/tasks 的任何修订，逐条记在此处：改前原文 / 改后原文 / 依据 / 谁裁定。）

### 2026-11-20 对抗验证后修订（主循环按裁决全量处置；裁决文件 `%TEMP%/adversarial-review-20261120-170151/裁决-全量.json`，blocking 7 / misleading 4 / falsePositive 1；攻击打包文件已拆分为独立主张补复核 0010-0014）

1. **ShaderProcessor 根因（blocking 1，复核 0001/0002#1#2/0005/0006/0008 合并）**：改前——proposal/design/spec/tasks 把 ShaderProcessor/Main.cpp 当"全仓唯一调用点"，要求包 try/catch 并跑 exe 验收、0.1③ 核其 build.ninja DEFINES；改后——全仓零活跃调用方（ShaderProcessor 不在根 CMakeLists add_subdirectory、build.ninja 零命中、无 exe，其调用写的是全仓不存在的 fileloading_utils::LoadStringFile，构建外死代码）；spec 删「唯一调用方必须处置失败」整条要求并在读取要求加注；tasks 1.3 改为零调用方事实存档（验收=工件"唯一调用"0 命中+证据四项输出）、0.1③ 改为构建外事实核实；design Context/Goals/Risks/Non-Goals 同步；本批不碰 ShaderProcessor。依据：复核 verified（根 CMakeLists 57-92 无它、两份 build.ninja 零命中、git grep fileloading_utils 仅 Main.cpp:18 且无命名空间声明）。
2. **test_output 全清砸跨 run 累积（blocking 2，复核 0007）**：改前——D3/spec/tasks 2.1"删除目录下既有文件"；改后——只清 *.log，保留 json/png（C# 像素判据多 run 对账与跨后端 PNG 对比的前提）；spec 增「跨运行累积产物不被清理」场景；tasks 2.1 增累积保留判据③；D7/tasks 6.2/proposal 回归口径增"至少一条 C# 像素判据脚本全链路"。依据：check_csharp_*.py 缺一份报告即非零（docs/testing.md 144-207 规定多次 headless 各写 csharp_* 再对账）。
3. **改名红态不可达（blocking 3，复核 0002#3）**：改前——D5/tasks 3.1/spec 场景"改名 .redbak 后重跑构建"；改后——红态走源码侧（临时注释该头 --emit-binding 调用 + 改名 + 构建 + 还原四步），spec 场景 WHEN 改"期望产物检查时缺失只剩改名件"的行为契约。依据：build.py 先逐头无条件 --emit-binding、CodeGen 无条件覆写、闸门在其后；build.py 注释自述"红态构造只能走源码侧"。
4. **零活跃前提不成立（blocking 4，复核 0002#4/0004）**：改前——D6/tasks 4.1③"当前零活跃 change（如当前）…非零退出"；改后——本 change 自己即唯一活跃，③改为旧形状自动探测兼容判据，零活跃/多活跃分支走读证明并如实登记不谎称实测。依据：openspec/changes 下除 archive 仅本 change（13 条未勾）。
5. **5.1 判据失效（blocking 5，复核 0002#5）**：改前——"旧串 git grep 0 命中、新路径 Test-Path 为真"；改后——旧串还命中 check_binding_baseline.py docstring 与两份归档 evidence；修复扩位到该 docstring（新增 D8），归档件不改写；判据收窄为点名两文件的 scoped grep 0 命中（改前 3 命中红）；Test-Path 恒真不作判据。依据：git grep -F 旧串实测命中点。
6. **TODO 销账冲突（blocking 6，复核 0012）**：改前——proposal 销账条"四条整条删除（含前缀通配）"；改后——三条整删 + 前缀通配条只删前半段、孤儿产物后半段保留为独立条目（与 tasks 5.2/design Non-Goals 一致）。依据：docs/TODO.md 该条目同写前缀通配与 codegen 只写不删两件事，孤儿产物用户未点头收入。
7. **capture 越界触发范围不一致（blocking 7，复核 0013）**：改前——spec 限 headless 判"帧号越界"，D4/tasks 2.3/proposal 不限模式判"未写出"；改后——统一为"headless 运行中，captureFrame>=0 且未写出（帧号越界或回读无效）"，交互不发逐测试警告（误用由参数校验警告覆盖）；spec 增「交互有效帧号不误报」场景。依据：非 headless 除 TestReadback 外不写图，按旧 tasks 写法帧号有效也误报。
8. **快照排序（misleading 1，复核 0002#6）**：改前——取快照只写在 6.2 描述里，无独立任务；改后——新增 0.2（主循环首次构建前执行），6.2/D7 以 0.2 为锚。依据：照原清单顺序执行 fc /b 变成当前比当前恒绿。
9. **spin_check CLI 兼容（misleading 2，复核 0009）**：改前——D6"位置参数或 --change"未定形状，轮次位置参数（argv[3]/argv[2]）去向不明；改后——D6/tasks 4.1 钉死保留轮次位置参数 + 可选 --change 旗标，增旧形状兼容判据③；CLAUDE.md/rationale 引用的调用形状不变、无需改。依据：spin_check.py 现有 argv 结构与 CLAUDE.md:92/rationale:43-44 引用。
10. **proposal Why#3 与 D4 矛盾（misleading 3，复核 0010）**：改前——Why#3"非 headless 带 --capture 静默 noop"；改后——如实写"除 TestReadback 单帧写图路径外无提示，其余测试只建 RT 不写图"，并注明旧表述被实测修正。依据：TestReadback frames 三目式与写图点不在 headless 门内（复核另补前提：非 headless 不创建 test_output、WriteReadbackPNG 不查 stbi 返回值——已写进 D4 警告文本前提与 Risks，该静默失败本身另案记 TODO）。
11. **场景措辞改动未登记（misleading 4，复核 0014）**：改前——csharp-bindings 增量把既有场景「期望产物前缀/未列入期望前缀」改为「期望产物清单/未列入期望清单」，proposal 只登记闸门句+新增场景；改后——proposal Modified Capabilities 明登术语同步，本条即改前改后对照。依据：两份场景逐字比对仅差该词；属与闸门句一致的必要术语修订，不放松判据。
12. **falsePositive 登记（复核 0011）**：攻击主张"每个支持 capture 的测试都在 headless 门外消费 capture"被证伪——五测试写图点（WriteReadbackPNG）均在门内（Main.cpp 242/449/676/897/1046 行段），门外只建回读 RT 不产出 PNG，TestIMGUI 全在门内。处置：不改设计；仅把警告措辞从"消费"精确为"写出 PNG"（并入第 7/10 条的文字修订）。
