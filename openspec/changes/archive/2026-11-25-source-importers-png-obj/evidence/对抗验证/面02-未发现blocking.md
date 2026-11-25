# 面 02：`Texture2DFactory` / `MeshFactory` / `AssetFactoryBase` / `AssetDatabase` / `AssetFormatException` —— 未发现 blocking

审查对象：`design.md:7`（资产层设施）、`design.md:36`（D4 工厂路径）、`tasks.md:14-16`。

## 一、五个符号全部真实

| 符号 | 真实落点 | 结果 |
|---|---|---|
| `AssetFactoryBase` | `Scripting/Managed/Assets/AssetFactoryBase.cs:19` `public abstract class AssetFactoryBase : ResourceFactoryBase`；静态队列 `Pending`（`:23`）、`Enqueue`（`:29`）、`Dequeue`（`:39`） | ✓ |
| `AssetDatabase` | `Scripting/Managed/Assets/AssetDatabase.cs:19` `public sealed class AssetDatabase`；`Create`（`:32`）、`Load`（`:49`）、`SaveAll`（`:39`） | ✓ |
| `AssetFormatException` | `Scripting/Managed/Assets/AssetFormatException.cs:14` `public sealed class AssetFormatException : Exception`；`AssetPath`（`:17`）+ 两构造（`:19` / `:25`） | ✓ |
| `Texture2DFactory` | `Scripting/Managed/Assets/Texture2DFactory.cs:10` `public sealed class Texture2DFactory : AssetFactoryBase`；`TypeName = "Texture2D"`（`:13`） | ✓ |
| `MeshFactory` | `Scripting/Managed/Assets/MeshFactory.cs:10` `public sealed class MeshFactory : AssetFactoryBase`；`TypeName = "Mesh"`（`:13`） | ✓ |

生成侧依赖也真实：`ResourceFactoryBase` 在 `out/build/x64-debug/codegen/bindings/resource_management__IResourceFactory.Bindings.cs:13`，`ResourceBase` 在 `resource_management__IResource.Bindings.cs:13`。文件路径清单与 `Scripting/Managed/Assets/` 目录实际内容逐项对得上（7 份 `.cs`）。

## 二、不阻断但需处置的 1 条 misleading

`design.md:36` 的首选路径「经 `Texture2DFactory`/`MeshFactory` 的 Create 入队 + `AssetDatabase` 出队填数据」缺少「谁注册这两个工厂」：全仓唯一的注册点在门控探针 `ResourceAssetProbe.RegisterFactories`（`ResourceAssetProbe.cs:262-271`，门控 `CASCADED_CSHARP_ASSET_PROBE`），而导入器又被规定「不调注册表」。按首选路径实现，门控开时 `CreateResource("Texture2D", ...)` 无工厂可查会抛错。D4 的第二个方案「直接 new 资产填数据交迭代器」真实可行（`TestCSharpImporter` 已证明），所以不判 blocking。完整证据与修法见：

- `主张-存在-004.json`
- `证据-存在-004.md`

## 三、结论

除上述 1 条 misleading 外，本面五个符号与它们的既有行为全部真实存在，**未发现 blocking**。
