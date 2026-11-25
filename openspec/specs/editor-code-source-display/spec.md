# editor-code-source-display

## Purpose

代码型资产的源文件显示：显示提供者必须可按资源类型加路径注册且资源类型自决展示，shader-library 提供者隐去 shLib 显示其实际编译源路径，C# 脚本经注册显示资源隐去 dll 只显示手写 cs 源，代码型源显示必须有可失败的验收且回归零差异、绑定产物不变。

## Requirements

### Requirement: 显示提供者必须可按资源类型加路径注册且资源类型自决展示

`AssetDisplayProviders` MUST 支持 `Register(kind, path, provider)`（path 空＝种类默认，兼容单参）与 `Resolve(kind, path)`（精确 (kind,path)→种类默认→默认提供者）。`EnumerateAssets` MUST 按 `Resolve(kind, relative)` 取提供者。展示什么由资源类型自己决定。**生产注册点 MUST 钉死真实路径**：`(shader-library, "VulkanShaderLibrary.shLib")` 与 `(shader-library, "D3D12ShaderLibrary.shLib")` 绑 ShaderSourceDisplayProvider；探针 `ShaderLibraries/Browser.shLib`（路径不同）MUST 落默认提供者仍显（TestEditorAssetBrowser 不受影响）。MUST 有 off 开关 `CASCADED_EDITOR_SOURCE_DISPLAY=off`（默认 on，登记 README）：off 时不做源显示注册，shLib 落默认仍显。本能力 MUST NOT 注册改变 texture/mesh 展示行为的提供者（数据型展示由它们自己的提供者定，本批不写其要求）。

#### Scenario: 按类型加路径精确绑定与探针落默认

- **WHEN** 注册 (shader-library, "VulkanShaderLibrary.shLib") 后，对 VulkanShaderLibrary.shLib 与 ShaderLibraries/Browser.shLib 各 Resolve
- **THEN** 前者 MUST 返回 ShaderSourceDisplayProvider；后者（探针，路径不同）MUST 落默认提供者；off 开关时 VulkanShaderLibrary.shLib MUST 落默认（shLib 仍显）

#### Scenario: 数据型不被本能力注册

- **WHEN** 核对本能力注册的提供者种类
- **THEN** MUST NOT 含 texture/mesh 种类的行为改变提供者；texture/mesh MUST 仍走默认提供者（本能力未给它们注册），本能力 MUST NOT 写数据型展示行为要求

### Requirement: shader-library 提供者必须隐去 shLib 显示其实际编译源路径

MUST 有 `ShaderSourceDisplayProvider`（kind=shader-library）。**源映射 MUST 从 shLib 字节读**（PathHash 序列化即源路径串，`m_ShaderFiles` 键与 `ShaderFileInfo.path`）：主线程读 shLib 字节提取实际编译进库的源路径列表（相对 CAResources），映射源文件 `CAResources/<路径>.slang`，填进 AssetContext.SourceFiles；提供者 MUST 纯投影（不碰文件系统/不调门面），MUST NOT 产 shLib 自身条目，MUST 产每个源路径一条 AssetEntry（**仅 path+kind 两字段**：kind=shader-source，path=相对工程根；提供者全权浏览器忠实渲染，D5）。**缺源文件的历史路径**（真实库当前 0 条、夹具构造）MUST 仍产条目（kind=shader-source-missing），MUST NOT 静默跳过。两 shLib 产源条目 MUST 按 path 去重。

#### Scenario: shLib 隐去实际源路径显现

- **WHEN** 临时工程根预置写死源路径串的 shLib（CAAssets）+ 对应 .slang 源，注册提供者后 list-assets
- **THEN** 结果 MUST NOT 含 shLib 条目；MUST 含 shLib 字节里每个源路径对应的源条目，path（相对工程根）/kind=shader-source 逐条等于写死期望；源条目数 MUST 等于写死值（非「==源文件数」恒等式）；条目 MUST 仅 path+kind 两字段

#### Scenario: 缺源历史路径不静默跳过

- **WHEN** shLib 含一条无对应源文件的历史路径
- **THEN** MUST 产该条目（kind=shader-source-missing），MUST NOT 静默丢弃；调用方 MUST 能从条目看到略去了什么（源路径）

#### Scenario: 两 shLib 去重

- **WHEN** 临时根预置两个 shLib（Vulkan/D3D12）含重叠源路径
- **THEN** 源条目 MUST 按 path 去重无重复；缺源 kind MUST 保留

### Requirement: C# 脚本必须经注册显示资源隐去 dll 只显示手写 cs 源

MUST 有「显示资源注册表」`RegisterDisplayResource(kind, path, provider)`，浏览器 MUST 额外对注册显示资源调 Describe 引入条目（不依赖 path 在 CAAssets）。MUST 注册 (csharp-script, "Scripting.Managed.dll", CSharpSourceDisplayProvider)。该提供者 MUST NOT 产 dll 条目，MUST 扫 `Scripting/` 手写 *.cs（**排除 obj/ 与生成产物**）产源条目（**仅 path+kind**：kind=csharp-source，path=相对工程根）。**128 生成绑定源（out/build/<preset>/codegen/bindings）本批 MUST 明示略去 + 给取回目录路径**，MUST NOT 静默隐藏。主线程扫源填 context，提供者纯投影。

#### Scenario: dll 隐去手写 cs 源显现

- **WHEN** 注册 C# 显示资源 + 临时工程根预置手写 .cs（含 obj 干扰文件），list-assets
- **THEN** 结果 MUST NOT 含 dll 条目；MUST 含每个手写 .cs 源条目（path 相对工程根/kind=csharp-source）逐条等于写死期望；obj 生成 .cs MUST NOT 出现；dll 不在 CAAssets 也 MUST 引入源条目

#### Scenario: 生成绑定源明示略去可取回

- **WHEN** 核对本能力对生成绑定源（codegen/bindings）的处置
- **THEN** MUST 有标记说明略去了生成绑定源 + 取回目录路径（out/build/<preset>/codegen/bindings）；MUST NOT 无标记静默隐藏

### Requirement: 代码型源显示必须有验收且回归零差异绑定不变

MUST 新增 tester `TestCodeSourceDisplay`（独立进程，进 --list 不进全量；**构建先于跑测试**）：临时工程根隔离，断言 shLib/dll 隐去 + 源条目逐条写死期望（path 相对工程根/kind，仅两字段）+ 源条目数写死 + 两 shLib 去重 + 缺源 kind + obj 排除；红态 `CASCADED_EDITOR_SOURCE_DISPLAY=off`→shLib 落默认仍显（同进程差分）、源根非法→显式错误。断言落点 MUST 为 list-assets 结果（tester C++ 读，无需新 C# 测试工程）。MUST 有钉死临时夹具的截图（dock 显源条目无 shLib/dll，核「共 N 条」写死读数，CapturePreviewAsync 不前置 AGENTS §2）+ read_image。现有 22 用例 + CoreTests 全绿；**TestEditorAssetBrowser 独立跑仍过**（探针落默认）；绑定产物逐字节不变；check_csharp_host_readme 0 问题。

#### Scenario: 视觉验收源条目无程序集

- **WHEN** 钉死临时夹具（CASCADED_EDITOR_ASSET_ROOT + PROJECT_ROOT，小表全可见）交互打开资产浏览器
- **THEN** dock MUST 显示源条目（kind=shader-source/csharp-source）、「共 N 条」等于写死值，MUST NOT 显示 shLib/dll 程序集条目；CapturePreviewAsync 抓窗经 read_image 核对，MUST NOT 置前/最小化/移动用户窗口

#### Scenario: 回归与绑定零差异

- **WHEN** 完成后跑现有 22 用例 + TestEditorAssetBrowser 独立 + CoreTests + 绑定基线 + check_csharp_host_readme
- **THEN** 现有 22 用例 name+status MUST 与基线零差异、TestEditorAssetBrowser MUST 仍过、CoreTests MUST exit 0 + PASSED 26、绑定产物 MUST 逐字节不变（0/0/0）、check_csharp_host_readme MUST 0 问题、--list MUST 含 TestCodeSourceDisplay（26）且全量仍 22
