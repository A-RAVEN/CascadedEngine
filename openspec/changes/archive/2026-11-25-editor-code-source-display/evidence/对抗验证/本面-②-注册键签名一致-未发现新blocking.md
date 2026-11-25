# 本面-②：注册键签名三处一致，未发现新 blocking

- 攻击面：工件一致性（面②：Register(kind,path)/Resolve(kind,path) 三处签名）
- 结论：design、spec、tasks 三处签名逐字一致，**本面未发现新的 blocking**；该面真正的阻断项（注册装配无归属、「提供者未注册」红态不可构造）已由并行面 `主张-影响-001` 与 `主张-验收-007` 落盘，本条不重复。

## 一、三处签名逐字对照

design :20（D1）：

> `AssetDisplayProviders` 增 `Register(string kind, string path, IDisplayProvider provider)`（path 空＝该种类默认，兼容旧 `Register(kind, provider)`）；`Resolve(string kind, string path)` 先查 (kind, path) 精确绑定，落回种类默认，再落 `DefaultFileDisplayProvider`。

spec :5：

> `AssetDisplayProviders` MUST 支持 `Register(kind, path, provider)`（path 空＝该种类默认，兼容仅种类注册）与 `Resolve(kind, path)`（先精确 (kind,path) 绑定，落回种类默认，再落默认提供者）。

tasks :10（1.1）：

> 注册键扩类型+路径 `Scripting/Managed/Editor/AssetDisplay.cs`：增 `Register(kind, path, provider)`（path 空＝种类默认，兼容旧 Register(kind,provider)）+ `Resolve(kind, path)`（精确 (kind,path)→种类默认→DefaultFileDisplayProvider）。

一致的三个要素：① 参数顺序 (kind, path, provider)；② path 空＝种类默认；③ 回落顺序 精确 (kind,path) → 种类默认 → DefaultFileDisplayProvider。

显示资源注册表也一致：design :24 `RegisterDisplayResource(kind, path, provider)`；spec :33 同名同参；tasks :11 同名同参。

## 二、现有代码不构成冲突

```text
PS> Select-String -Path 'Scripting/Managed/Editor/AssetDisplay.cs' -Pattern 'internal static void Register','internal static IDisplayProvider Resolve'
Scripting\Managed\Editor\AssetDisplay.cs:117:        internal static void Register(string kind, IDisplayProvider provider)
Scripting\Managed\Editor\AssetDisplay.cs:127:        internal static IDisplayProvider Resolve(string kind)
```

现有只有单参版本；本 change 增的是两个不同元数（2 参 vs 3 参）的重载，C# 不会歧义；唯一调用点 `EditorService.cs:1124` 由 tasks 2.3 改成 `Resolve(kind, relative)`，没有遗漏的 `Resolve(kind)` 调用者（全仓 grep 只有这一处）。

## 三、本面真正的阻断项（已由并行面落盘，不重复）

1. `主张-影响-001`：tasks 2.1 只写 `ShaderSourceDisplayProvider` 的行为，不写它在哪个文件、用哪个键注册；全仓零个 `AssetDisplayProviders.Register` 调用点。读法 A（按种类默认注册）会让 TestEditorAssetBrowser 变红；读法 B（按路径绑两个真实 shLib）漏注册则 shLib 照显、特性静默不生效。两种读法都能符合字面，且都逃过 4.1 的 22 条回归。
2. `主张-验收-007`：红态「提供者未注册→落默认」不可构造——现有注册表没有 Unregister/Reset/Clear，也没有任何自检开关；`CASCADED_EDITOR_SELFTEST` 现有值里没有与显示提供者相关的项。
3. `主张-验收-008`：tasks 1.1「精确绑定优先种类默认（写死两路径断言）」与 1.2「登记表可查」没有可执行落点（仓库无 C# 测试工程，tester 只能观察 WebSocket list-assets）。

## 四、结论

签字面：**通过**（无新 blocking）。要修的不是签名，而是注册装配与可关闭性；修改建议见 `主张-影响-001` / `主张-验收-007`。
