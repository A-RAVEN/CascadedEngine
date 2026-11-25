# 面 01：`ResourceImporterBase` / `ResourceImporterProductionsBase` —— 未发现 blocking

审查对象：`design.md:5`、`design.md:20`、`design.md:24`、`tasks.md:14-15`、`spec.md:5/19` 点名的 `ResourceImporterBase` / `ResourceImporterProductionsBase` / `ResourceImporterProbe.cs`。

## 一、生成的反向基类真实存在

两份生成物都在 Debug 构建目录（CodeGen `--emit-binding` 产物，源树里没有是正常现象）：

| 生成文件 | 大小 / 修改时间 | 关键行 |
|---|---|---|
| `out/build/x64-debug/codegen/bindings/resource_management__ResourceImporter.Bindings.cs` | 16261 B / 2026-11-24 17:08:42 | `:13 public abstract class ResourceImporterBase`；`:16 GetTags()`；`:19 GetSourceDirectory()`；`:22 GetDestDirectory()`；`:25 Produce()` 返回 `ResourceImporterProductionsBase` |
| `out/build/x64-debug/codegen/bindings/resource_management__ResourceImporterProductions.Bindings.cs` | 12849 B / 2026-11-24 17:08:42 | `:13 public abstract class ResourceImporterProductionsBase`；`:16 HasNext()`；`:19 NextPath()`；`:22 NextResource()` 返回 `ResourceBase` |

命令（命中逐字）：

```powershell
Select-String -Path 'out/build/x64-debug/codegen/bindings/resource_management__ResourceImporter.Bindings.cs' -Pattern 'public abstract class|public abstract string|public abstract ResourceImporterProductionsBase'
# 13:    public abstract class ResourceImporterBase
# 16:        public abstract string GetTags();
# 19:        public abstract string GetSourceDirectory();
# 22:        public abstract string GetDestDirectory();
# 25:        public abstract ResourceImporterProductionsBase Produce();

Select-String -Path 'out/build/x64-debug/codegen/bindings/resource_management__ResourceImporterProductions.Bindings.cs' -Pattern 'public abstract class|public abstract bool|public abstract string|public abstract ResourceBase'
# 13:    public abstract class ResourceImporterProductionsBase
# 16:        public abstract bool HasNext();
# 19:        public abstract string NextPath();
# 22:        public abstract ResourceBase NextResource();
```

C++ 侧的来源也真实：`GeneralResources/header/CAResource/ResourceImporter.h:49 CA_BIND_REVERSE()` + `:50 class ResourceImporter`（`:60 Produce()`）；`:69 CA_BIND_REVERSE()` + `:70 class ResourceImporterProductions`（`:75-80 HasNext/NextPath/NextResource`）。

## 二、先例 `ResourceImporterProbe.cs` 的门控与用法逐条对得上

| 工件说法 | 真实落点 | 结果 |
|---|---|---|
| 门控 `CASCADED_CSHARP_IMPORTER_PROBE=1`（默认关） | `ResourceImporterProbe.cs:27` 常量、`:224` `if (... != "1") return;` | ✓ |
| 源目录自声明 env | `ResourceImporterProbe.cs:97` `Environment.GetEnvironmentVariable("CASCADED_CSHARP_IMPORTER_SOURCE") ?? string.Empty` | ✓ |
| `Produce` 造资源入迭代器 | `ResourceImporterProbe.cs:105-131`（`ProbeProductions` 队列、两条写死字节 `{C5,01}`/`{C5,02}`） | ✓ |
| `ProbeImporter : ResourceImporterBase` | `ResourceImporterProbe.cs:84` | ✓ |
| `ProbeProductions : ResourceImporterProductionsBase` | `ResourceImporterProbe.cs:59` | ✓ |

## 三、附注（不构成阻断）

- 生成物只在 `x64-debug` 预设下就位；`out/build/x64-relWithDebugInfo/codegen/bindings/` 目前只有 `IWindow.Bindings.cs`（旧产物）。tasks 4.2 的构建命令是 `python build.py --config Debug`，`Scripting.Managed.csproj` 的默认 `CascadedBindingDir` 也指向 `out/build/x64-debug/codegen/bindings`，所以执行者按 tasks 走不会缺文件；若改用 RelWithDebInfo，需先构建让生成物就位。
- 生成基类位于 `Scripting.Generated.Bindings.ResourceManagement` 命名空间，`Scripting/Managed` 内可直接继承（`ResourceImporterProbe.cs:6-7` 已 `using` 这两个命名空间）。

结论：**本面未发现 blocking**。
