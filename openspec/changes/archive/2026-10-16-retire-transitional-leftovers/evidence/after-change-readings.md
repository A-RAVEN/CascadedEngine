# 改动后的读数（`retire-transitional-leftovers` 任务 7.2）

```powershell
python build.py --config Debug
out\build\x64-debug\bin\CoreTests.exe
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --headless 10 --report %TEMP%\rtl_final_vulkan.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12  --headless 10 --report %TEMP%\rtl_final_d3d12.json
```

| 项 | 改动前（`pre-change-readings.md`） | 改动后 | 结论 |
|---|---|---|---|
| `python build.py --config Debug` | 退出码 0；`重跑 0 条、跳过 76 条`；`codegen 12.2s / 编译 3.1s / 合计 15.3s`；`BUILD SUCCESSFUL` | 退出码 **0**；`重跑 67 条、跳过 9 条（输入没变的那些）`；`codegen 48.9s / 编译 89.3s / 合计 138.2s`；**`BUILD SUCCESSFUL`**；`CODEGEN FAILED` 0 处、`error C` 0 处 | 通过（重跑 67 条是因为这次改了 `HashKeys.h` 与 `CodeGenHashFwd.h` —— 它们是 67 条清单行的输入；**跳过的 9 条与新增的 9 条一一对应**，说明删掉清理壳之后"输入没变就跳过"的账本仍然准） |
| `CoreTests.exe` | 退出码 0；`PASSED$` 行 **12**；`FAILED` 行 0 | 退出码 **0**；`PASSED$` 行 **12**；`FAILED` 行 **0** | 通过（逐项相同） |
| vulkan tester | 退出码 0；8 条 status 全 pass | 退出码 **0**；8 条 status 全 pass；报告 `%TEMP%\rtl_final_vulkan.json` | 通过 |
| d3d12 tester | 退出码 0；8 条 status 全 pass | 退出码 **0**；8 条 status 全 pass；报告 `%TEMP%\rtl_final_d3d12.json` | 通过 |

两次运行各写一份独立报告（`--report`），不再共用 `test_output/result.json` —— 那个文件每次运行都被截断重写，
跑完第二个后端就只剩它那一份（这条是本 change 的对抗验证审查提出来的）。

## 本 change 的判据汇总（逐条可核）

| 判据 | 读数 |
|---|---|
| tasks 2.1：`-- build.py` 限定下两个名字 0 处 | **0 处**（改动前 4 处）；`build.py` 1125 → 1083 行 |
| tasks 2.2：删后构建与测试 | 见上表（`BUILD SUCCESSFUL` + 12/0） |
| tasks 2.3：没删过头 | 单节开关 16 行、守卫脚本存在、`SubmitAndWait` 2+4+2+0 处、`CA_PRIVATE_REFLECTION` 5 处注释都在 |
| tasks 3.1：`HashKeys.h` 里"待删的头" | **0 处**（改动前 1 处） |
| tasks 3.2：`Hasher.h` 的提及 | `CodeGenHashFwd.h` **0 处**（改动前 1 处）；`HashKeys.h` **2 处**（3.1 改写后的正文 + 那句历史陈述，与 design D2 一致） |
| tasks 4.1：六份 `Purpose` | `Update Purpose after archive` 全仓 **0 处**（改动前 6 处） |
| tasks 4.2：delta 场景数对账 | 五条 `MODIFIED` 全部相等（2/2、10/10、3/3、2/2、2/2），见 `delta-reconciliation.md` |
| tasks 4.3：delta 点名的代码出处 | 五处逐个命中（含 `CodeGenMain.cpp` 里"无序容器"0 处），见 `delta-reconciliation.md` |
| tasks 4.4：`openspec validate retire-transitional-leftovers` | **valid** |
| tasks 5.1 / 5.2：README | 新增第十二节（写清单节开关承载哪条判据、守卫的三个退出码、删了会怎样）；章节号 零…十二 **无重复**（改动前两个「五、」）；`verify_readme.py` → `20 documented command(s) run, 0 mismatch(es)` |
| tasks 7.1：三份 delta 的旧符号命中分类 | 8 行命中 = 8 行分类表（防回流守卫 3 / 历史理由 5，无第三类），见 `specs-stale-statements.md` |

## 产物新鲜度核对（回答"测试用的是不是旧的 dll / exe"）

判据不是"我觉得新"，而是三条能核的事实（2026-10-16 实测）：

1. **重跑构建，ninja 判定无工作可做**：`python build.py --config Debug` → 两次 `ninja: no work to do.`
   （codegen 工具一次、引擎一次）+ `codegen: 重跑 0 条、跳过 76 条（输入没变的那些）`。
   ninja 的规则是"输出比输入旧才重建"，"无工作可做"就等于**每个目标的产物都不比它的输入旧**。
2. **产物的 mtime 晚于最后一次代码改动**：本 change 最后改代码是 `CACore/header/HashKeys.h` 18:51:25
   （`CodeGenHashFwd.h` 18:51:19、`build.py` 18:50:28）；`bin/` 下的项目产物全是 18:55:29–18:55:50 ——
   `ThreadManager.dll` 18:55:29、`WindowSystem.dll` 18:55:30、`CAGeneralReourceSystem.dll` 18:55:31、
   `ShaderCompilerSlang.dll` 18:55:31、`IOManager_FS.dll` 18:55:31、`VulkanRenderBackend.dll` 18:55:40、
   `GPUBackendTester.exe` 18:55:46、`CoreTests.exe` 18:55:49、`D3D12RenderBackend.dll` 18:55:50。
3. **生成的产物全部早于二进制**：`out/build/x64-debug/codegen/` 下 76 份 `*.codegen.h`，最新一份 18:54:20，
   **没有一份晚于** `CoreTests.exe`（18:55:49）—— 编进去的生成头就是二进制里那一版。
   （其中最早一份是 10-15 14:25:32：那是"输入没变就跳过"账本判定跳过的 9 条之一，产物沿用上一轮，
   是设计行为，不是陈旧。）

边界：`bin/` 里的第三方 DLL（`libclang.dll` / `slang.dll` / `mimalloc*` / `glfw3.dll` / `Jolt.dll` /
`assimp-vc145-mt.dll` / `DirectXTex.dll` 等）本来就是 10-05 拉下来的预编译件，不参与本轮改动；
`CodeGen.exe` 是 10-11 的，但这次没有改 `Tools/CodeGen/CodeGenMain.cpp`，"无工作可做"同样覆盖它。
模块 DLL 由 `CACore/private/CaModuleManager.cpp` 与 `LibraryLoader.cpp` 用 `LoadLibrary(modulePath)` 装载，
路径来自测试器自身所在目录 —— 也就是上面这批 18:55 的 DLL。

**用这批产物重跑**：`CoreTests.exe` 退出码 0、`PASSED$` **12** 行、`FAILED` **0**；vulkan 与 d3d12 各退出码 0、
各 **8/8 pass**（各自独立 `--report`）。跑完再核时间戳：四个关键产物的 mtime 一个没变（测试不产出二进制）。

