# 改动前的读数（`retire-transitional-leftovers` 任务 1.1）

代码状态：提交 `72e84a5`（工作区另有 `docs/TODO.md` 与 `TODO/README.md` 的文档改动，与本次代码无关）。
日期：2026-10-15。

## ① 源码树里旧命名的 include = 0 处

```powershell
git grep -n -E '#include\s+"[^"]+\.(Reflection|Serialization|Hash)\.h"' -- '*.h' '*.hpp' '*.cpp' |
  Where-Object { $_ -notmatch '^openspec/' }
```

输出：空（0 处）。

## ② 构建目录里旧命名的产物 = 0 个

```powershell
Get-ChildItem 'out\build' -Recurse -File -Include *.Reflection.h,*.Serialization.h,*.Hash.h -ErrorAction SilentlyContinue
```

输出：空（0 个文件）。

## ③ 那个过渡装置的引用点 = 4 处，全在 `build.py` 里

```powershell
git grep -n -E 'cleanup_legacy_codegen_products|CODEGEN_LEGACY_SUFFIXES'
```

输出（逐字）：

```
build.py:182:CODEGEN_LEGACY_SUFFIXES = (".Reflection.h", ".Serialization.h", ".Hash.h")
build.py:185:def cleanup_legacy_codegen_products() -> tuple[int, int, list[str]]:
build.py:208:            for name in CODEGEN_LEGACY_SUFFIXES:
build.py:600:    legacy_removed, legacy_presets, legacy_left = cleanup_legacy_codegen_products()
```

行数口径（实测，供 tasks 2.1 对照）：函数体 185–215 行 = **31 行**；常量与函数连同注释 182–215 行 = **35 行**；
加调用点（600）与打印行（601–605）= **41 行**。

**结论**：它今天没有任何东西可删（①②都是 0），只在 `build.py` 内部被引用（③）—— 每次构建都在空转。
按 tasks 1.1 的门禁，三条读数都符合预期，本 change 的前提成立。

## 改动前的构建与测试读数（供 7.2 逐项对照）

```powershell
python build.py --config Debug
out\build\x64-debug\bin\CoreTests.exe
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --headless 10 --report %TEMP%\rtl_pre_vulkan.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12  --headless 10 --report %TEMP%\rtl_pre_d3d12.json
```

| 项 | 读数 |
|---|---|
| `python build.py --config Debug` | 退出码 **0**；`codegen: 重跑 0 条、跳过 76 条`；`[耗时] configure 0.0s / codegen 12.2s / 编译 3.1s / 合计 15.3s`；**`BUILD SUCCESSFUL`** |
| `CoreTests.exe` | 退出码 **0**；以 `PASSED` 结尾的行 = **12**；含 `FAILED` 的行 = **0** |
| vulkan tester | 退出码 **0**；`result.json` 8 条、status 全部 `pass` |
| d3d12 tester | 退出码 **0**；`result.json` 8 条、status 全部 `pass` |

## 删掉过渡装置之后的读数（tasks 2.2）

```powershell
python build.py --config Debug
out\build\x64-debug\bin\CoreTests.exe
git grep -n -E 'cleanup_legacy_codegen_products|CODEGEN_LEGACY_SUFFIXES' -- build.py
```

| 项 | 读数 |
|---|---|
| 构建 | 退出码 **0**；`codegen: 重跑 0 条、跳过 76 条（输入没变的那些）`；`[耗时] configure 0.0s / codegen 6.9s / 编译 3.1s / 合计 10.0s`；**`BUILD SUCCESSFUL`** |
| `CoreTests.exe` | 退出码 **0**；以 `PASSED` 结尾的行 = **12**；含 `FAILED` 的行 = **0** |
| `build.py` 行数 | 改动前 **1125** 行 → 改动后 **1083** 行，删掉 **42** 行（装置本体 41 行 + 与上一节之间的空行 1 行） |
| tasks 2.1 的判据 | `-- build.py` 限定下 **0 处**（改动前 4 处） |

删后复查（tasks 2.3，逐项带文件限定）：`CodeGenMain.cpp` 里单节开关 `--emit-reflection` / `--emit-serialization` /
`--emit-hash` 命中 **16** 行；`Tools/CodeGen/check_legacy_symbols.py` 存在；`SubmitAndWait` 在
`GeneralResources/header/CAResource/IResource.h` **2** 处、`IOManager_FS/IOManager.cpp` **4** 处、
`Test/CoreTests/ResourceBatchTest.cpp` **2** 处、`Test/GPUBackendTester` **0** 处（与改动前一致）；
`CA_PRIVATE_REFLECTION` 那 5 处解释性注释（`CodeGenMeta.h` 1 + `CodeGenMain.cpp` 2 + `TestPrivate.h` 2）都在。
`PROJECT_DIR` 仍有 22 处使用，没有留下孤儿符号。

