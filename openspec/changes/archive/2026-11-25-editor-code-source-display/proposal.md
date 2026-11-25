# Proposal: editor-code-source-display

## Why

资产浏览器（editor-asset-browser）现在把 `.shLib` 当一条显示（kind=shader-library），但 shLib 是多个 `.slang` 源文件编译来的程序集——用户看不到里面每个源文件。C# 脚本同理（dll 是程序集，源是 `.cs`）。用户裁定（2026-11-25）：**代码型资产不显示程序集本身，只显示源文件**；且**展示什么由资源类型自己决定**（资产中心多态）——要展示一个资源就注册一个显示提供者，指明资源类型和路径。这正是 `IDisplayProvider`（editor-asset-browser design D4）预留的 v2 方向：接口形状（Describe→条目树、children）已就位，本批把它用起来。

**范围纪律（用户明令）**：「隐程序集显源文件」只针对**代码型**（shader/C#）。贴图/模型怎么展示由它们自己的提供者决定，**本批不预设、不写要求**（现在落默认提供者只是框架兜底）。

## What Changes

- **显示提供者注册改为按「资源类型 + 路径」**：现 `AssetDisplayProviders.Register(kind, provider)` 只按种类；改为可指明类型+路径绑定具体资源的提供者，`Resolve` 按类型+路径取（落回种类默认）。资源类型自决展示。
- **框架支持「注册的显示资源」**：提供者注册可引入显示条目（不依赖该文件在资产根 CAAssets 里被枚举到）——C# 的 dll 在 bin/ 不在 CAAssets，靠注册引入其源文件展示。
- **shader-library 提供者**：输入 shLib（类型+路径）→ **不产 shLib 条目**，改产 `CAResources/Shaders/` 下各 `.slang` 源文件条目（扫源目录约定；shLib 的 m_ShaderFiles 键是源路径哈希非路径串，C# 侧拿不到源路径串，故扫源目录）。
- **C# 脚本提供者**：注册（csharp-script, Scripting.Managed.dll）→ 不产 dll 条目，改产 `Scripting/` 下各 `.cs` 源文件条目。
- **浏览器枚举整合**：list-assets 收集「资产根文件枚举条目」+「注册显示资源条目」，代码型程序集隐去、源文件显现。

## Capabilities

### New Capabilities

- `editor-code-source-display`: 代码型资产源文件显示——显示提供者按类型+路径注册、资源类型自决展示、代码型隐程序集显源文件（shader-library→.slang、csharp-script→.cs）、注册显示资源引入条目。

### Modified Capabilities

- `editor-asset-browser`：显示提供者注册键从「仅种类」扩到「类型+路径」；list-assets 枚举整合注册显示资源条目；代码型程序集条目隐去。（其余 list-assets 七字段/assets-changed/只读边界不变。）

## Impact

- **代码**：`Scripting/Managed/Editor/AssetDisplay.cs`（注册键扩类型+路径、Resolve 重载）；新增 `Scripting/Managed/Editor/DisplayProviders/`（ShaderSourceDisplayProvider.cs / CSharpSourceDisplayProvider.cs）；`EditorService.cs` ExecuteListAssets 整合注册显示资源；门控/源根约定。
- **不动**：IResource/反序列化核心；GPUTexture/Mesh 绑定面；shLib/dll 二进制格式；贴图/模型展示行为（本批不预设）；list-assets 七字段形状与 assets-changed 协议。
- **依赖**：复用 IDisplayProvider/AssetEntry/AssetContext（editor-asset-browser）；源目录约定 CAResources/Shaders（16 .slang）/ Scripting（38 .cs）。

## Non-goals

- **贴图/模型展示行为**：由它们自己的提供者决定，本批不写要求（用户明令不擅自扩展）。
- **程序集内嵌溯源元数据**（shLib 记源路径串）：属身份重设计批；本批源映射走扫源目录约定。
- **源文件编辑/编译触发**：本批只展示源文件条目，不做编辑/重编译。
- **缩略图/预览**：后续批。
- **C# 按类型展开**（dll 反射出类型树）：本批展 .cs 源文件，不按反射类型。