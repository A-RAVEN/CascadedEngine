# 本面未发现 blocking：回归基线路径真实存在（面 5）

- 攻击面：工件一致性 · 面 5「4.1 回归基线点名路径是否真实存在」
- 审查对象：`tasks.md` 4.1（第 25 行）、`tasks.md` 2.2（第 15 行）、`specs/editor-web-render-spike/spec.md` 要求 3（第 33 行）
- 结论：**本面未发现 blocking**；发现 1 条 misleading（`主张-一致-011`：新用例进不进全量顺序跑没写，4.1 比较口径不唯一）。

## 一、点名路径逐项核到

| 4.1 点名 | 核到的实物 | 读数 |
|---|---|---|
| 归档 `2026-11-24-editor-asset-browser/evidence/5.1-全量-*.json` | `openspec/changes/archive/2026-11-24-editor-asset-browser/evidence/5.1-全量-vulkan.json`、`5.1-全量-d3d12.json` | 各 22 条、22 条全 `pass`、均不含 `TestEditorAssetBrowser`（独立进程用例不在全量报告里） |
| `CoreTests exit 0 + PASSED 26` | `.../evidence/5.1-CoreTests.txt` | `Select-String -Pattern 'PASSED'` 命中 26 行；`docs/testing.md:29` 记录当前计数 26 |
| 「绑定基线对账 缺失0/不一致0/多余0」 | `Tools/CodeGen/check_binding_baseline.py`（:128 打印该读数） | 当前锚 `openspec/changes/archive/2026-11-11-csharp-bindings-namespace-alignment/evidence/基线产物哈希.txt` 存在（`Test-Path` 为 True） |
| 归档 `2026-11-24-editor-asset-browser/evidence/5.1-基线.txt` | 存在 | 内容「基线对账：缺失 0 / 不一致 0 / 多余 0」 |

## 二、命令与真实输出

```text
PS> $j=Get-Content 'openspec\changes\archive\2026-11-24-editor-asset-browser\evidence\5.1-全量-vulkan.json' -Raw | ConvertFrom-Json
vulkan total=22 pass=22 含AssetBrowser=False
PS> $j2=Get-Content '...\5.1-全量-d3d12.json' -Raw | ConvertFrom-Json
d3d12 total=22 pass=22
PS> (Select-String -Path '...\5.1-CoreTests.txt' -Pattern 'PASSED' | Measure-Object).Count
26
PS> Test-Path 'openspec\changes\archive\2026-11-11-csharp-bindings-namespace-alignment\evidence\基线产物哈希.txt'
True
```

## 三、非 blocking 观察（未立案为本面 blocking）

1. `主张-一致-011`（misleading）：tasks 2.2 只写「独立进程跑，照 TestEditorAssetBrowser D8 先例」，漏了先例的另一半「进 --list 但不进全量同进程顺序跑」（`docs/testing.md:42`、`Main.cpp` --list 段注释）；4.1 因此没有写死比较对象与 `--list` 新计数（当前 23，本批后应为 24）。
2. `tasks.md` 4.2 写「回写 TODO/008 未决问题 1（零拷贝互操作成熟度）」，而 `TODO/008-free-dock-editor.md:57` 的未决问题 1 原文是「**Vulkan↔WebGPU 共享纹理互操作成熟度**」。本 spike 只验 D3D12，写进去只能部分回答该问题；4.2 的验收「TODO/008 已更新」也没有写更新后要含什么（例如「D3D12 侧结论 + Vulkan 仍未核」）。这属于验收可核性问题，不构成本面 blocking。
3. 4.1 用「归档 2026-11-24-editor-asset-browser/…」这种省略 `openspec/changes/archive/` 前缀的写法，与归档任务 5.1 的既有写法一致（同样省略前缀），不判缺陷。

## 四、结论

面 5 没有 blocking：4.1 的四个点名对象（基线 json、CoreTests 读数、绑定基线读数、归档基线 txt）全部真实存在且读数与期望一致。需要处置的只有 011 的比较口径与上面两条非 blocking 观察。
