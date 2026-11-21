# Proposal: failure-visibility-hygiene（失败显式化与验证卫生小批合并）

## Why

用户 2026-11-20 裁定：FileLoader 单项改动太小，把几个同族小修复合并成一个 change。合并主题：**让错误/失败看得见**——静默返回空、静默丢写、日志串味、闸门恒绿、工具读错清单，都是"缺陷存在但信号缺失"的同族问题，判据形状相同（缺陷态必红、修复后必绿）。六个条目全部于 2026-11-20 对现状逐条实测验真（非陈旧账）：

1. `CACore/private/FileLoader.cpp` 三函数（`LoadStringFile`/`LoadBinaryFile`/`WriteBinaryFile`）打不开文件时静默返回空/丢写（`is_open()` 假分支直接跳过）；**全仓零活跃调用方**——`ShaderProcessor/Main.cpp` 不在构建（根 CMakeLists 无其 add_subdirectory、build.ninja 零命中、无 exe），且其调用写的是全仓不存在的 `fileloading_utils::LoadStringFile`（构建外死代码，本批不碰）。ROADMAP 阶段 5 方向段第二项。
2. `Test/GPUBackendTester/Main.cpp` headless 只建 `test_output/` 不清理（`createDirectoryIfNeeded("test_output")`），跨后端同名日志串味（docs/TODO.md 有实测记录）。
3. capture 两条 LOW：非 headless 带 `--capture` 无任何提示——除 `TestReadback` 的单帧写图路径外，其余测试只建回读 RT 不写图，用户无从知道 capture 没生效（2026-11-20 实测修正：TestReadback 交互也跑 1 帧且写图点不在 headless 门内，"静默 noop" 的旧表述不准确）；capture 帧号越界除 `TestReadback` 外静默不写（docs/TODO.md）。
4. `build.py` 逐接口产物期望检查仍是前缀通配 `glob(f"{stem}.Bindings.*")`，改名件（`.redbak`）可绕过闸门（精确文件名只用在登记层）（docs/TODO.md）。
5. `Tools/spin_check.py` 把 tasks/evidence 路径写死在已归档的 `2026-09-20-engine-codegen-framework` 上，轮界队列装置对任何新 change 失效（docs/TODO.md）。
6. 文档修复：`docs/testing.md` 基线路径与 `Tools/CodeGen/check_binding_baseline.py` docstring 的当前基线/对照路径均指向未归档目录（实际已归档到 `archive/2026-11-11-csharp-bindings-namespace-alignment/`，2026-11-20 对抗裁决 blocking 5 扩位）；`docs/ROADMAP.md` 阶段 5 目标句「也不要要求」笔误。

## What Changes

- **FileLoader 失败显式化**：新增 `cacore::file_error`（派生 `std::runtime_error`，定义于 `CACore/header/FileLoader.h`，消息形状与 `ca_io::io_error` 一致：动作+路径+原因）；三函数打不开/读不足/写失败/flush 失败一律抛出点名路径；空文件合法（返回空，不抛）；函数签名不变。零活跃调用方，失败语义由 CoreTests 红绿钉死；ShaderProcessor 死代码本批不碰（另案）。**不复用 `ca_io::io_error`**：CACore 是底层静态库，`Interface/IOManager/header/IOManager/IOManager.h` 拉 ThreadManager 依赖，反向依赖不可接受（design D1）。
- **test_output 跑前清理**：headless 启动时删除 `test_output/` 下既有 `*.log`（保留目录与 json/png 跨运行累积产物——C# 像素判据多 run 对账与跨后端 PNG 对比依赖它们），打印清理计数（design D3）。
- **capture 误用显式化**：非 headless 带 `--capture` 在参数校验阶段 stderr 警告（沿用 TODO 原改法"打提示"；不拒绝——实测 `TestReadback` 交互模式也跑 1 帧且其写图点不在 headless 门内，拒绝会砍掉真实可用路径；其余测试写图点均在 headless 门内、门外只建回读 RT，design D4 记依据与前提）；**headless 运行中** capture 未写出（帧号越界或回读无效）在该测试帧循环结束后 stderr 警告点名测试与帧号，交互运行不发逐测试警告（`TestReadback` 既有 throw 不动，全模式）。
- **绑定产物闸门改精确文件名**：`build.py` 逐接口期望检查从 `<stem>.Bindings.*` 通配改为按 0.1 清点的每 stem 实际产物形状精确核对（`.Bindings.cs`/`.Bindings.cpp`），改名件必被闸门自己点名拒绝。
- **spin_check 按 change 定位**：去掉写死常量，`queue`/`evidence` 支持显式 change 名或自动取唯一活跃 change；已归档 change 可回退归档路径；零个/多个活跃且未显式指定 ⇒ 非零退出点名（design D6）。
- **文档修复**（第 6 项 + 裁决 blocking 5 扩位）：docs/testing.md 基线路径、`Tools/CodeGen/check_binding_baseline.py` docstring 的当前基线/对照路径（同串过期）补归档前缀；ROADMAP 笔误。归档 evidence 里的历史路径串不改写（design D8）。
- **销账**：修好的 docs/TODO.md 条目按该文件规则处理——capture LOW、spin_check、test_output 三条整条删除；**前缀通配条只删本批修掉的前半段，孤儿产物后半段保留为独立条目并改写注明**（用户未点头收入本批，与 tasks 5.2 / design Non-Goals 一致）。

## Capabilities

### New Capabilities

- `fileloader-error-channel`：FileLoader 三函数的失败显式化（新增规范见 `specs/fileloader-error-channel/spec.md`）。
- `tester-output-hygiene`：GPUBackendTester 的 test_output 跑前清理与 capture 误用/越界显式化（新增规范见 `specs/tester-output-hygiene/spec.md`）。

### Modified Capabilities

- `csharp-bindings`：MODIFIED「跨头文件接口投影必须有构建期已知接口清单」——期望产物存在性检查从前缀通配改精确文件名；既有场景「清单来自实际标记而非部分期望产物」术语同步（「期望产物前缀」→「期望产物清单」，与闸门句一致，改前改后见 tasks 修订记录）；新增改名件拒绝场景（全文替换见增量）。

### 不进 spec（工具/文档，tasks 跟踪）

- spin_check 参数化（开发工具，无既有能力目录；docstring 同步更新）；两处一行文档修复；TODO 销账。

## Impact

- 产品：`CACore/header/FileLoader.h`、`CACore/private/FileLoader.cpp`、`Test/GPUBackendTester/Main.cpp`、`build.py`（逐接口期望检查段）、`Tools/spin_check.py`、`Test/CoreTests/`（新增 FileLoader 红绿用例；CoreTests 链接 CACore 静态库，已实测 `target_link_libraries(... PRIVATE CACore)`）。**不碰 `ShaderProcessor/`**（构建外死代码）。
- 文档：`docs/testing.md`（基线路径）、`Tools/CodeGen/check_binding_baseline.py`（仅 docstring 路径）、`docs/ROADMAP.md`（笔误）、`docs/TODO.md`（销账：三条整删 + 前缀通配条前半段）、FileLoader 对外文档（与代码同目录，CACore 下）、tester `--help` 文本、spin_check docstring。
- 行为变化（故意的）：FileLoader 失败从静默变抛出（零活跃调用方，语义由 CoreTests 钉死）；test_output 跑前清 *.log（json/png 跨运行累积产物保留）；非 headless + capture 从静默变 stderr 警告（继续运行，交互 TestReadback 单帧写图路径保留）。
- 回归口径：`python build.py --config Debug` 通过；CoreTests 全绿（含新用例）；两后端 headless 17/17；至少一条 C# 像素判据脚本全链路通过（证清理不砸跨 run 对账）；bindings 基线 0/0/0 + codegen 产物逐字节不变（本批不碰生成器与序列化运行时，快照为独立任务 0.2、首次构建前由主循环取）。
- 明确不做（Non-Goals，见 design）：孤儿产物清理（TODO 前缀通配条的后半段，用户未点头收入）；22 处悬空 workflow 脚本（删/修待用户裁定）；CodeGen 冷跑退出码 6；D3D12 假异步读回；哈希扫描盲区。
