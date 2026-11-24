# 面 04：tester 独立进程用例机制 —— 未发现阻断项

## 一、`--list` 与 `--test` 都真实存在

`Test\GPUBackendTester\Main.cpp` 的 `--list` 分支：

```cpp
9767: 	// --list
9768: 	if (listOnly)
9769: 	{
9770: 		std::cout << "Available tests:\n";
...
9810: 		// editor-asset-browser 4.1：第 23 项（只读资产浏览器：list-assets / assets-changed / 显示提供者）。
9811: 		// **独立进程跑**（design D8：编辑器服务是进程级一次性状态机，与 TestEditorWindowPanel 不能同进程），
9812: 		// 所以它进了清单但**不进下面的全量顺序跑**，只按 `--test TestEditorAssetBrowser` 跑。
9813: 		std::cout << "  TestEditorAssetBrowser\n";
9814: 		return 0;
9815: 	}
```

`--list` 的 `std::cout << "  Test` 行共 23 条（命令：`(Select-String -Path Main.cpp -Pattern 'std::cout << "  Test' | Measure-Object).Count` → 23）。

`--test` 单跑分发：

```cpp
10154: 		else if (strcmp(testName.c_str(), "TestEditorAssetBrowser") == 0)
10155: 		{
10156: 			// 独立进程用例（design D8）：只走这条单跑分发，不排进下面的全量顺序跑。
10157: 			runTestWithDiagnostics("TestEditorAssetBrowser", TestEditorAssetBrowser);
10158: 		}
```

## 二、D8 先例在归档 design 里

`openspec/changes/archive/2026-11-24-editor-asset-browser/design.md`：

> ### D8 编辑器服务一次性，新用例独立进程
>
> EditorService 是进程级一次性状态机：`s_state==4`（Shutdown 落 4）直接 return，无复位路径（EditorService.cs:135/289）。既有 TestEditorWindowPanel 收尾 CASCADED_EDITOR_STOP=1 停服务，全量共用同一进程/托管运行时。**两个编辑器用例无法同进程共存**（一个停了服务另一个起不来）。故 `TestEditorAssetBrowser` MUST 在**独立进程**跑（`GPUBackendTester.exe --test TestEditorAssetBrowser`，新鲜服务）；全量顺序跑保持既有 22 用例（含 TestEditorWindowPanel），新用例不进同进程顺序跑。--list 仍列 23（清单完整），run-all 与独立跑分开。5.1 回归口径 accordingly：既有 22 同进程零差异 + 新用例独立进程过。不改服务生命周期（复位涉及窗口/scene 重建与 hostfxr 关闭语义，超本批）。

同 change 的 `5.1-全量-d3d12.json` 与 `5.1-全量-vulkan.json` 各 22 条，正好对应“全量 22 + 独立 1”；`TestEditorAssetBrowser` 不在全量里。

## 三、`TestEditorAssetBrowser` 函数本身

```cpp
5973: // ---- TestEditorAssetBrowser（editor-asset-browser 4.1，第 23 用例）----
...
6361: void TestEditorAssetBrowser(TestContext& ctx)
```

`tasks.md` 2.2 要求新用例“Main.cpp + --list 登记，独立进程跑，照 TestEditorAssetBrowser D8 先例”，与现有机制一致：新增函数、加 `--list` 一行、加 `--test` 分发一条，不排进全量顺序跑。

## 四、注记（不构成阻断）

- “独立进程”不是用例内部再 spawn 一个进程，而是用 `GPUBackendTester.exe --test <名字>` 单独起一个 tester 进程；这是 D8 的原话口径。
- 新用例登记实际有三处：函数、`--list`、`--test` 分发。`tasks.md` 2.2 写“Main.cpp + --list 登记”，没有逐条列三处，但机制本身真实，不算符号缺失。

## 五、结论

本面未发现阻断项。tester 的 `--list`、`--test`、`TestEditorAssetBrowser` 先例与 D8 独立进程口径都真实存在。
