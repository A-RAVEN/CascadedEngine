# 面 02：数字溯源（像素惯例 / PASSED 26 / 绑定基线 0-0-0 / 22）——未发现 blocking

> 只读核对；未运行构建或测试程序。以下读数为 2026-11-24 实测。与存在面 `面05-未发现blocking.md`、`面07-未发现blocking.md` 的读数一致，不重复立 claim。

## 一、`(255,0,0,255)` 像素惯例

工件原文：

- `design.md:7`「像素读回惯例 (255,0,0,255) 精确匹配（editor-window-panel scene view 证明）」
- `design.md:39`「... == (255,0,0,255)（或写死纯色期望）」
- `tasks.md:14`「离屏读回中心 == (255,0,0,255)」
- `tasks.md:20`「中心像素 ... == (255,0,0,255)」
- `spec.md:10`「中心像素 MUST == (255,0,0,255)」

出处（逐字）：

- `Test/GPUBackendTester/Main.cpp:5534`「判据：精确 (255,0,0,255)（按 backbuffer 格式判色序）」
- `Main.cpp:5599` `const bool backbufferRed = red == 255 && green == 0 && blue == 0 && bytes4[3] == 255;`
- `Main.cpp:4615` `return r == 255 && g == 0 && b == 0 && a == 255;`（`ReadCaptureCenterRed`；`Main.cpp:4858` 由 `TestEditorWindowPanel` 调用）
- `docs/testing.md:49`「视口中心精确 `(255,0,0,255)`」
- 归档 `2026-11-24-editor-asset-browser/evidence/3.1-3.3-绿态核对.txt:31-35` 列出 `Main.cpp` 的 2340/2348/7288/7340 四处 `(255,0,0,255)`。

命令与输出（节选）：

```text
4615: return r == 255 && g == 0 && b == 0 && a == 255;
4858: const bool red = ReadCaptureCenterRed(ctx, readings, pixelDetail);
5534: // 合成/虚拟显示，headless 也可跑。判据：精确 (255,0,0,255)（按 backbuffer 格式判色序）。
5599: const bool backbufferRed = red == 255 && green == 0 && blue == 0 && bytes4[3] == 255;
```

结论：数字可溯源，不是凭空发明；design 的「editor-window-panel scene view 证明」与 `TestEditorWindowPanel` 的 ⑨/⑩ 像素证据对得上。

## 二、`PASSED 26`

工件原文：`tasks.md:25`「CoreTests exit 0 + PASSED 26」；`spec.md:43`「CoreTests MUST exit 0 + PASSED 26」。

出处：

- `docs/testing.md:29`「`PASSED` 行数当前为 **26**」；`:235`「CoreTests 退出码 0 且 `PASSED` 26 行」。
- 归档 `2026-11-24-editor-asset-browser/evidence/5.1-CoreTests.txt`：`PASSED` 行数 = 26，全 122 行。

命令与输出：

```powershell
$f='openspec/changes/archive/2026-11-24-editor-asset-browser/evidence/5.1-CoreTests.txt'
"count=$((Select-String -Path $f -Pattern 'PASSED').Count) total=$((Get-Content $f).Count)"
```

```text
count=26 total=122
```

结论：26 可溯源；spike 只在 tester 增用例，不改 CoreTests，26 仍是本批应写死的数。工件没写出处，但数字不是发明；执行者可用上述命令复算。

## 三、`缺失0/不一致0/多余0` 与 `22` 基线

- `tasks.md:25`「绑定基线对账 缺失0/不一致0/多余0」；归档 `5.1-基线.txt` 原文「基线对账：缺失 0 / 不一致 0 / 多余 0」。
- `tasks.md:25` 的基线 `5.1-全量-*.json`：`5.1-全量-d3d12.json` 22 条、`5.1-全量-vulkan.json` 22 条。

命令与输出：

```text
5.1-全量-d3d12.json = 22
5.1-全量-vulkan.json = 22
基线对账：缺失 0 / 不一致 0 / 多余 0
```

结论：数字可核；与存在面 面07 一致。

## 四、结论

本面未发现 blocking。`(255,0,0,255)`、`PASSED 26`、`0/0/0`、`22` 都能用命令打到唯一出处；没有凭空发明的数字。

唯一相关的缺陷是 `画面尺寸` 被写成「写死」却没有数字，已并入 `主张-溯源-003`（形状/尺寸判据不写死），不在本面重复。
