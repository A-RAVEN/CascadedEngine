# 面 08：归档基线 `4.1-全量-*.json` —— 未发现 blocking

审查对象：`tasks.md:25`（4.1「现有用例同进程两后端 name+status 与基线（归档 2026-11-24-editor-web-render-spike/evidence/4.1-全量-*.json）零差异……CoreTests exit 0 + PASSED 26……绑定基线 check_binding_baseline.py 缺失0/不一致0/多余0」）。

## 一、基线文件真实存在

目录：`openspec/changes/archive/2026-11-24-editor-web-render-spike/evidence/`

| 文件 | 大小 | 内容 |
|---|---|---|
| `4.1-全量-vulkan.json` | 1839 B | JSON 数组，22 条 |
| `4.1-全量-d3d12.json` | 1832 B | JSON 数组，22 条 |

`4.1-全量-vulkan.json` 头两行原样：

```json
[
  {"name":"TestSimpleTriangle","status":"pass","duration_ms":157,"exit_code":0},
  {"name":"TestTriangleWithConstantColor","status":"pass","duration_ms":74,"exit_code":0},
```

字段核对（命令与读数）：

```powershell
$j = Get-Content '.../4.1-全量-vulkan.json' -Raw | ConvertFrom-Json
@($j).Count                                   # 22
@($j | Where-Object { $_.name -and $_.status }).Count   # 22（全部含 name/status）
```

D3D12 同样 22 条、全部含 `name`/`status`。`name`+`status` 正是 tasks 4.1 要比对的字段；文件路径通配 `4.1-全量-*.json` 恰好命中这两份。

## 二、同一条任务里的其它点名物也真实

| 点名物 | 实测 |
|---|---|
| `4.1-CoreTests.txt`（CoreTests exit 0 + PASSED 26） | 同目录文件存在；`Select-String -Pattern 'PASSED'` 计数 **26**（末两行为 `FileLoader error channel test PASSED` / `Bindings reverse projection test PASSED`） |
| `Tools/CodeGen/check_binding_baseline.py` | `Test-Path` → True |
| `docs/testing.md`（--list 同步） | `Test-Path` → True |

## 三、附注（不构成阻断）

- 基线 22 条是「同进程全量」用例清单；本批 3.1 的 `TestSourceImporters` 是独立进程用例（照 `TestEditorAssetBrowser` 先例），按惯例不进全量同进程顺序跑，否则 4.1 的「现有用例与基线零差异」会因多出一条而失真。工件已写「独立进程」，但没写「不进全量同进程」这半句；实现时按既有先例处理即可，不影响本面「基线真实存在」的结论。
- `4.1-全量-*.json` 记录的是 editor-web-render-spike 归档时点的读数（22 条），与本 change 完成时点之间若已有其它 change 增删用例，比对前需要确认基线仍对应「现有用例」；这是一致性面的事，不在本面展开。

## 四、结论

`openspec/changes/archive/2026-11-24-editor-web-render-spike/evidence/4.1-全量-*.json` 两份都真实存在，字段与 tasks 4.1 的用法对得上；**本面未发现 blocking**。
