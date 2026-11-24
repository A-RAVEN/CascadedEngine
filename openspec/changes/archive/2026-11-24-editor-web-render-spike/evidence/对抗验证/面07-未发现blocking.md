# 面 07：归档基线 `5.1-全量-*.json` —— 未发现阻断项

## 一、路径真实存在，通配符匹配两份

`tasks.md` 4.1 写的基线：

> 现有用例同进程两后端 name+status 与上批基线（归档 2026-11-24-editor-asset-browser/evidence/5.1-全量-*.json）零差异；CoreTests exit 0 + PASSED 26（evidence/4.1-CoreTests.txt）；绑定基线对账 缺失0/不一致0/多余0（evidence/4.1-基线.txt，spike 不碰生成器/绑定面）。

实际目录 `openspec/changes/archive/2026-11-24-editor-asset-browser/evidence/` 下的匹配文件：

```text
5.1-全量-d3d12.json
5.1-全量-vulkan.json
```

通配符 `5.1-全量-*.json` 命中 2 份，路径前缀是 `openspec/changes/archive/`（任务里写“归档 …/evidence/…”是简写，能唯一落到这两份）。

## 二、内容就是 name+status 的 22 条

`5.1-全量-d3d12.json` 开头与结尾：

```json
[
  {"name":"TestSimpleTriangle","status":"pass","duration_ms":149,"exit_code":0},
  ...
  {"name":"TestCSharpWindowCallbacks","status":"pass","duration_ms":12,"exit_code":0}
]
```

计数：

```powershell
$d3 = Get-Content '...\5.1-全量-d3d12.json' -Raw | ConvertFrom-Json
$vk = Get-Content '...\5.1-全量-vulkan.json' -Raw | ConvertFrom-Json
"d3d12=$($d3.Count) vulkan=$($vk.Count)"
```

输出：

```text
d3d12=22 vulkan=22
```

22 条正好是“既有全量同进程用例”（`TestEditorAssetBrowser` 不在里面，它是独立进程用例）；与 `tasks.md` 4.1 的“现有用例同进程两后端”口径一致。

## 三、同目录的 CoreTests 与绑定基线也对得上

- `5.1-CoreTests.txt`：用 Unicode 读取后数 `PASSED` 行，共 26 行（命令：`Select-String -Path ... -Encoding Unicode -Pattern 'PASSED' | Measure-Object` → 26），与 `tasks.md` 4.1 的“PASSED 26”一致。
- `5.1-基线.txt` 原文：`基线对账：缺失 0 / 不一致 0 / 多余 0`，与 `tasks.md` 4.1 的“缺失0/不一致0/多余0”一致。

## 四、结论

本面未发现阻断项。归档基线 `5.1-全量-*.json` 真实存在两份，各 22 条 name+status；同目录的 CoreTests 26 与绑定基线 0/0/0 也真实可读。
