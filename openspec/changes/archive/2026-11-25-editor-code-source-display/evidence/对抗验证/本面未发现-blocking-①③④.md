# 本面未发现 blocking：① 数字 / ③ dll 位置 / ④ 源根

> 本文件说明这三面为什么没有判 blocking，避免复核者把 misleading / falsePositive 当成漏报。

## ① 数字溯源（主张-溯源-001 / 002 / 003）

- 16：`CAResources/Shaders` 递归 .slang 实数 16，全在 Shaders 下、外部 0 份。属实的数字。
- 38：`Scripting` 递归 .cs 实数 38（含 2 份 obj 生成物，手写 36）。数字本身没编；问题在「编译来」的因果（002 misleading）。
- 26：CoreTests 归档日志正好 26 行 PASSED；`SerializerTest.cpp` 的 main 逐段跑，26 条可数。
- 22：run-all 段 22 条 + 两份基线 JSON 各 22 条；`--list` 当前 25 条（含 3 条独立进程），design D7 少写「同进程」。
- 结论：没有凭空编造的数字；001 的库路径不一致（Vulkan 21 条）按「文档不准确」记 misleading，不按「数字造假」判 blocking，因为 16 这个数在源目录上成立，只是不能推出「两份库都由它编译来」。若复核者认为「库与源不一致」会导致 5 条 shader 静默不可见，可与 011 合并升为 blocking。

## ③ dll 位置（主张-溯源-005）

- `Scripting.Managed.dll` 实测在 `out/build/x64-debug/bin/`（另有 `managed/` 副本与源工程 bin/obj 中间产物）；CAAssets 顶层只有 CSharpProbe + 两份 shLib，递归 dll 计数 0。
- design 的「在 out/build/<preset>/bin/（不在 CAAssets）」与实测一致，falsePositive，无 blocking。

## ④ 源根 / 扫描根（主张-溯源-006）

- `CAResources/Shaders` 存在、`Scripting` 存在、CAResources 顶层只有 Images/Models/Shaders，均属实。
- 「与 ShaderImporter 扫描根一致」不成立（扫描根是 CAResources，上一级），但当前 Shaders 之外 0 份 .slang，所以现有 16 份的文件集合与扫描结果恰好一致；该偏差今天不会让任何真实 shader 消失。
- 结论：记 misleading（设计论证不准确、未来加文件会静默漏），不判 blocking；若复核者要求「扫描根一致」是 MUST，则应升为 blocking 并加差集断言。
