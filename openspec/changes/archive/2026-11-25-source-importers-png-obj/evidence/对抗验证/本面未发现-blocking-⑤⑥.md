# 本面未发现 Blocking：⑤ 资产浏览器联动、⑥ 截图视觉验收

本文件记录这两面**没有 Blocking** 的核查依据，避免把「有 misleading 主张」误读成「有阻塞项」。

## ⑤ 资产浏览器联动

核查过的事实：

- `.tex`→`texture`、`.mesh`→`mesh` 的扩展名映射真实存在：`Scripting/Managed/Editor/AssetDisplay.cs:111-112`；当前规范 `openspec/specs/editor-asset-browser/spec.md:44` 要求覆盖 `.shLib/.tex/.mesh/.res`。
- `list-assets` 命令、`AssetEntry.kind/registered` 字段与 `TestEditorAssetBrowser` 的 WebSocket 对账机制已在仓库里（`Test/GPUBackendTester/Main.cpp:6363` 起、`AssetDisplay.cs:137` `KindOf`）。
- 因此「kind=texture/mesh 条目」不是无中生有，断言可失败（条目缺失或 registered=false 会红）。

没有 Blocking 的原因：本条的问题只是断言没有点名「哪条」（path/nodeId/sizeBytes），存在用探针资源顶替导入产物的假绿路径（详见 `主张-验收-011.json` 与 `证据-验收-011.md`），不需要阻塞整个 change；补上点名条目即可。

## ⑥ 截图视觉验收

核查过的事实：

- `CapturePreviewAsync` 抓 dock 的现成开关存在：`CSharpHost/README.md:428` 的 `CASCADED_EDITOR_SELFTEST=capture-dock=<png>`，且明确「不置前/不最小化/不移动用户窗口」。
- 编辑器 dock 的 `kind/registered` 显示由既有 `AssetDisplay` 与 `list-assets` 提供，不是新造 UI。
- 截图可失败：dock 空、缺条目、kind 错、registered=false 都能被 `read_image` 看出来。

没有 Blocking 的原因：截图是人工/主循环视觉核对，缺的是写死的期望条目而不是通道（详见 `主张-验收-012.json` 与 `证据-验收-012.md`）；按 AGENTS §2 的截图纪律补上「必须出现哪两行」即可，不需要阻塞。
