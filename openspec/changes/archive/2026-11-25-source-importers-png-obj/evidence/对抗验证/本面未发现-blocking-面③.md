# 本面未发现 blocking：③「纯 C# 无外部依赖」三条全部核到

> 本文件按「每条主张单独落盘；某一面没有 blocking 时也要留一份结论」的要求写。
> 攻击面：纯 C# / 无外部依赖 / 不动绑定面 / 反向接口限制是否属实。
> 时间：2026-11-24。

## 一、本面结论

本面共 3 条主张（主张-溯源-008 / 009 / 010），**全部为 falsePositive**（即工件的说法成立），**未发现 blocking**。

| 编号 | 主张 | 结论 | 关键出处 |
|---|---|---|---|
| 主张-溯源-008 | 「纯 C# 无外部依赖」是否空话 | 成立 | csproj 仅 net8.0 + 既有 WebView2 包；System.IO.Compression 属 .NET 8 共享框架，本机 8.0.x 三份都有该 DLL |
| 主张-溯源-009 | 「绑定产物逐字节不变」是否空头承诺 | 成立 | 绑定文件头「由 CodeGen --emit-binding 生成」；反向基类四个抽象方法；check_binding_baseline.py 按缺失/不一致/多余对账 |
| 主张-溯源-010 | 「C# 读不到管线扫描根」是否编的限制 | 成立 | ResourceImporter.h:27-28 明写；生成基类无访问器；管线只在原生侧 SetScanDirectories |

## 二、为什么这面没有 blocking

- 三条都是「工件声称的既有事实」；逐条用源码/头文件/生成产物/脚本核过后都能对上，没有发现凭空发明。
- 本面唯一值得带出的相关缺陷不在本面严重度里：`GetDestDirectory` 被写成「返回 ② 目标」，但管线不拿它决定落盘路径——那条记在 ⑤ 的**主张-溯源-016**（misleading），因为它属于「机制/验收口径」而非「纯 C# 依赖」。

## 三、复现命令（合并版）

```powershell
PS> Select-String -Path 'Scripting\Managed\Scripting.Managed.csproj' -Pattern 'TargetFramework|PackageReference'
9: <TargetFramework>net8.0</TargetFramework>
31: <PackageReference Include="Microsoft.Web.WebView2" Version="1.0.4370.26" />

PS> Select-String -Path 'out\build\x64-debug\codegen\bindings\ETextureFormat.Bindings.cs' -Pattern 'CodeGen'
1: // 由 CodeGen --emit-binding 生成，勿手改。

PS> Select-String -Path 'out\build\x64-debug\codegen\bindings\resource_management__ResourceImporter.Bindings.cs' -Pattern 'abstract'
16:        public abstract string GetTags();
19:        public abstract string GetSourceDirectory();
22:        public abstract string GetDestDirectory();
25:        public abstract ResourceImporterProductionsBase Produce();

PS> Select-String -Path 'GeneralResources\header\CAResource\ResourceImporter.h' -Pattern 'C# 反向实现读不到'
27:	//   * C# 反向实现读不到这两个值（托管代码没有入口），所以 C# 导入器 MUST 在自己的

PS> Test-Path 'Tools\CodeGen\check_binding_baseline.py'
True
```

## 四、边界

- 只读：grep / 读文件 / 本机共享框架目录列举；未构建、未跑 tester、未跑 CodeGen。
- 未核：运行时 DeflateStream 在 HostFxr 下的实际解析、绑定基线的两轮哈希对拍（都要跑构建，本轮禁止）。
