# 本面-④：registered 公式单父程序集下一致，未发现新 blocking

- 攻击面：工件一致性（面④：registered 语义 / D5 父程序集注册态）
- 结论：design、spec、tasks 对「单个父程序集」的 registered 公式逐字一致，**本面未发现新的 blocking**；该面的阻断项（字段语义与生效规范冲突、C#「程序集存在」无口径、真实根两个 shLib 的歧义）已由并行面落盘，本条不重复。

## 一、单父程序集下三处公式一致

design :38（D5）：

> 源文件条目：kind=shader-source/csharp-source；path/nodeId=相对工程根源路径；sizeBytes/modifiedUtc=源文件 FileInfo；**registered=父程序集是否注册**（shader 源条目 registered=HasResource(shLib relative)；C# 源条目 registered=dll 是否视为注册——dll 不在注册表，约定 registered=true 当程序集存在）。

spec :19：

> ……（kind=shader-source，path/nodeId=相对工程根源路径，sizeBytes/modifiedUtc=源文件 FileInfo，registered=父 shLib 是否 HasResource，children 空）。

spec :33：

> ……（kind=csharp-source，path/nodeId=相对工程根源路径，registered=true 当程序集存在）。

tasks :15（2.1）：

> ……（kind=shader-source，path/nodeId=相对工程根源路径，sizeBytes/modifiedUtc=源 FileInfo，registered=HasResource(shLib)，children 空）……

tasks :16（2.2）：

> ……（kind=csharp-source，registered=true 当程序集存在）……

对「一个 shLib」和「一个 dll」而言，三处说法一致：shader 源取父 shLib 的 HasResource；C# 源在程序集存在时取 true。**公式本身没有分歧**。

## 二、现有测试只覆盖单父程序集

```text
PS> Select-String -Path 'Test/GPUBackendTester/Main.cpp' -Pattern 'ShaderLibraries/Browser.shLib'
Test\GPUBackendTester\Main.cpp:6411:		{ "ShaderLibraries/Browser.shLib", 377, "shader-library", ...
```

TestEditorAssetBrowser 只预置一个 `.shLib`；tasks 3.1 的「临时工程根预置 shLib（CAAssets）」也是单数。单父场景下公式可判。

## 三、本面真正的阻断项（已由并行面落盘，不重复）

1. `主张-溯源-007`：把 registered 从「门面 HasResource 读数」改成「程序集存在」是改字段语义——PROTOCOL.md:127 与生效规范 :11/:16 都钉死 HasResource；dll 不在注册表，约定 true 等于字段不再表达「已注册」。
2. `主张-验收-004`：「程序集存在」没有探测口径——真实 dll 在 `out/build/x64-debug/bin/Scripting.Managed.dll`，注册 path 是裸名 `Scripting.Managed.dll`；按 Assembly.Location 恒 true，按工程根+注册 path 恒 false，两种口径给出相反期望。
3. `主张-验收-006` / `主张-影响-005`：真实 CAAssets 有两个 shLib（Vulkan/D3D12），每个 Describe 都扫同一源根 → 同一源文件两条同 nodeId 条目；去重后保留哪条、registered 取哪个父程序集没有规定，单 shLib 夹具测不到。
4. `主张-验收-005` / `主张-溯源-002`：`Scripting/` 递归包含 obj 下 2 个生成 .cs，影响 .cs 源条目集合（与 registered 无关，但同属源条目写死期望面）。

## 四、结论

公式面：**通过**（单父程序集下无新 blocking）。要修的是字段语义、C# 存在性口径与多父程序集规则；建议见上述并行主张。
