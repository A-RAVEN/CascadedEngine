# 面 05：`System.IO.Compression.DeflateStream` —— 未发现 blocking

审查对象：`design.md:28`（D2「IDAT（zlib 流，跳 2 字节 zlib 头用 `DeflateStream` 解压，忽略 4 字节 adler32）」）、`tasks.md:9`（1.1）、`spec.md:5`。

## 一、目标框架与真实程序集

```xml
Scripting/Managed/Scripting.Managed.csproj
    <TargetFramework>net8.0</TargetFramework>
```

`System.IO.Compression` 是 `Microsoft.NETCore.App` 共享框架的一部分，net8.0 目标不需要额外 `PackageReference`：

| 检查项 | 实测 |
|---|---|
| net8.0 参考程序集 | `C:\Program Files\dotnet\packs\Microsoft.NETCore.App.Ref\8.0.34\ref\net8.0\System.IO.Compression.dll`，20776 B（另有 8.0.23：21256 B） |
| 类型名在参考程序集里 | `cmd /c findstr /C:"DeflateStream" "<上述 8.0.34 ref 程序集>"` 退出码 0（FOUND） |
| 运行时程序集 | `C:\Program Files\dotnet\shared\Microsoft.NETCore.App\8.0.6\System.IO.Compression.dll` 264480 B；`8.0.23` 264504 B；`8.0.34` 264016 B |
| 已装运行时 | `dotnet --list-runtimes` 列出 `Microsoft.NETCore.App 8.0.6 / 8.0.23 / 8.0.34` |

```
Microsoft.NETCore.App 8.0.6 [C:\Program Files\dotnet\shared\Microsoft.NETCore.App]
Microsoft.NETCore.App 8.0.23 [C:\Program Files\dotnet\shared\Microsoft.NETCore.App]
Microsoft.NETCore.App 8.0.34 [C:\Program Files\dotnet\shared\Microsoft.NETCore.App]
```

## 二、与设计用法的一致性

`DeflateStream` 在 .NET 里处理的是**裸 deflate**（不带 zlib 头/adler32 尾），所以 design D2「跳过 2 字节 zlib 头、忽略 4 字节 adler32、把 IDAT 载荷喂 `DeflateStream`」的用法成立。PNG 的 zlib 包装头恒为 2 字节（CMF+FLG），adler32 恒为末 4 字节，这两点不是本仓自定义，是 PNG/zlib 规范；本面只核 `DeflateStream` 这个 BCL 类型真实存在。

## 三、结论

`DeflateStream` 在 net8.0 参考程序集与运行时程序集里都真实存在，无需新增依赖，**本面未发现 blocking**。
