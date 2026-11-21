# tester-output-hygiene Specification

## Purpose
约束 GPUBackendTester 的运行产物卫生：headless 启动前清理既有 `*.log` 而保留 json/png 等跨运行累积产物，使日志不跨运行、跨后端串味。同时约束 capture 的误用与帧越界必须向 stderr 显式报告，不得静默忽略。

## Requirements

### Requirement: headless 运行前必须清理 test_output 既有日志
GPUBackendTester 在 headless 启动时 MUST 删除 `test_output/` 目录下的既有 `*.log` 文件（保留目录本身，保留 `*.json`/`*.png` 等非日志产物），并打印清理计数；交互（非 headless）运行 MUST NOT 触发清理。目的：消灭跨运行/跨后端的同名日志串味（旧后端的 `*_validation.log` 残留被算到本次运行头上）。MUST NOT 删除 json/png：C# 像素判据脚本（`Tools/check_csharp_*.py`）依赖多次独立 headless 运行各自写出的 `csharp_*` 报告做对账，跨后端 PNG 对比依赖两次运行各自的截图——全清会砸掉这些跨运行累积判据。

#### Scenario: 哨兵旧日志被跑前清理
- **WHEN** 在 `test_output/` 种一个旧 `*.log` 文件后启动一次 headless 运行
- **THEN** 该旧日志 MUST 被删除，stdout MUST 打印清理计数；MUST NOT 残留上一次运行的同名日志

#### Scenario: 跨后端连跑日志不串味
- **WHEN** 先 vulkan 后 d3d12 连续 headless 运行
- **THEN** `test_output/` 内每个 `*.log` MUST 产生于第二次运行（时间戳落在其区间内）；MUST NOT 出现第一次运行写的同名日志残留

#### Scenario: 跨运行累积产物不被清理
- **WHEN** 第一次 headless 运行写出 `--report` 指定的 json 与截图 PNG，随后第二次 headless 运行（不同 report 名）启动
- **THEN** 第一次的 json 与 PNG MUST 仍在；第二次结束后两份 report MUST 同时存在（C# 像素判据的多 run 对账前提）

### Requirement: capture 误用与帧越界必须显式报告
非 headless 运行带 `--capture` 时 MUST 在参数校验阶段向 stderr 发出警告，说明 capture 的写图仅在 headless 生效、交互模式只有 `TestReadback` 的单帧路径会写出 PNG（且依赖 `test_output/` 目录已存在——非 headless 不创建该目录），其余测试只建回读 RT 不写图；随后继续运行。MUST NOT 静默忽略、也 MUST NOT 拒绝这条真实可用的交互路径。实测依据：`TestReadback` 的帧数为「`int frames = ctx.headlessFrames > 0 ? ctx.headlessFrames : 1;`」，其写图点不在 headless 门内；其余测试的写图点（`WriteReadbackPNG`）均在 headless 门内，门外的回读 RT 建立不产出 PNG。**headless 运行中**，某测试请求了 capture（`captureFrame >= 0`）但其帧循环结束后未写出 capture（帧号超出运行帧数或回读无效）时，MUST 向 stderr 警告并点名测试名与帧号（测试本身退出码不受影响）；交互运行不发该逐测试警告（误用已由参数校验阶段警告覆盖）。`TestReadback` 既有的「请求 capture 但读到 null/空白即 throw」语义 MUST 保留不变（全模式）。

#### Scenario: 非 headless 带 capture 有警告
- **WHEN** 不带 `--headless` 但带 `--capture <N>` 启动 tester
- **THEN** 参数校验阶段 stderr MUST 出现点名 capture 生效范围的警告；MUST NOT 静默忽略 capture 参数

#### Scenario: capture 帧越界有警告
- **WHEN** `--test TestSimpleTriangle --capture 999 --headless 1` 运行
- **THEN** stderr MUST 出现点名该测试与帧号 999 的警告；MUST NOT 静默不写

#### Scenario: 交互模式有效帧号不误报越界
- **WHEN** 非 headless 运行某测试（非 TestReadback）且 capture 帧号有效
- **THEN** MUST NOT 触发逐测试的「未写出 capture」警告（该警告仅限 headless）；参数校验阶段的误用警告照常

#### Scenario: TestReadback 既有语义不变
- **WHEN** TestReadback 请求了 capture 但回读为 null/空白
- **THEN** MUST 仍按既有实现 throw（本要求不放松也不改写该判据）
