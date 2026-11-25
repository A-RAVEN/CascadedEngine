# Design: editor-code-source-display

## Context

显示提供者（editor-asset-browser design D4，`Scripting/Managed/Editor/AssetDisplay.cs`）：`IDisplayProvider.Describe(AssetContext) → IReadOnlyList<AssetEntry>`；`AssetEntry` 七字段（nodeId/path/kind/sizeBytes/modifiedUtc/registered/children）；`AssetContext`（ResourceRoot/RelativePath/Kind/SizeBytes/ModifiedUtc/Registered）；`AssetDisplayProviders.Register(kind, provider)`/`Resolve(kind)`/`KindOf(ext)`。**契约（AssetDisplay.cs 注释）：提供者不碰文件系统、不调门面**——IO 在主线程命令里做完，提供者纯投影。

浏览器枚举（`EditorService.EnumerateAssets`，由 `ExecuteListAssets` 调）：`Directory.EnumerateFiles(资源根 CAAssets)` → 逐文件 `KindOf` + `HasResource` → `Resolve(kind).Describe` → 按 path 序数排序。生效规范口径：**nodeId=path=相对资源根**、条目与真实文件逐条一致、registered=HasResource 读数（PROTOCOL.md 2.6）。

**关键事实（对抗验证核实）**：
- `cacore::PathHash` 是字符串包装（`CACore/header/HashKeys.h` `struct PathHash` 含 `castl::string m_Name`，CodeGen `wrapper_traits<PathHash>` 序列化写 `obj.string()`）。**shLib 字节里直接含源路径串**（如长度前缀 + 「Shaders/FinalBlit」，相对 CAResources 扫描根）。**两真实 shLib 各 16 条 m_ShaderFiles 源路径、全映射到现有 .slang（缺源 0）**——按 m_ShaderFiles 条数提取才是 16；裸子串搜索会把 .hslang 包含文件等误计成 21（溯源面首轮误判，实现期实测纠正）。故**源映射可从 shLib 字节读**，不必扫源目录约定。
- ShaderImporter 扫描根是 **CAResources 全树**（`RenderBackend_Vulkan.cpp` 回退 `p/"CAResources"`），非仅 Shaders；今天 16 .slang 全在 Shaders/ 下是巧合。
- C# 程序集 `Scripting.Managed.dll` 在 `out/build/<preset>/bin/`（不在 CAAssets）；编译输入 = **36 手写 .cs（Scripting/，git 跟踪）+ 128 生成绑定 .cs（out/build/<preset>/codegen/bindings，csproj 显式 Include）**；Scripting/ 递归 *.cs 会含 2 个 obj 生成产物（须排除）。
- 真实 CAAssets 有**两个 shLib**（Vulkan 145710B / D3D12 293737B）+ 3 探针 .res。

## Goals / Non-Goals

**Goals:** 显示提供者按类型+路径注册、资源类型自决展示；代码型隐程序集显源文件（shader-library→shLib 内源路径、csharp-script→手写 .cs）；源条目路径基准扩协议；提供者纯投影（IO 主线程）。
**Non-Goals:** 贴图/模型展示行为（用户明令不预设）/源编辑编译/缩略图/C# 反射类型展开/生成绑定源显示（本批只手写源，明示+取回办法）。

## Decisions

### D1 注册键扩「类型 + 路径」+ 生产注册点钉死 + off 开关

`AssetDisplayProviders` 增 `Register(kind, path, provider)`（path 空＝种类默认，兼容旧单参）+ `Resolve(kind, path)`（精确 (kind,path)→种类默认→默认提供者）。`EnumerateAssets` 改调 `Resolve(kind, relative)`。
**生产注册点钉死**：启动时按真实路径注册 `(shader-library, "VulkanShaderLibrary.shLib", ShaderSourceDisplayProvider)` 与 `(shader-library, "D3D12ShaderLibrary.shLib", ShaderSourceDisplayProvider)`。**探针 `ShaderLibraries/Browser.shLib`（TestEditorAssetBrowser 夹具）路径不同 → 落默认提供者仍显**，既有第 23 用例不受影响。
**off 开关** `CASCADED_EDITOR_SOURCE_DISPLAY=off`（默认 on）：off 时不做源显示注册 → shLib 落默认仍显（红态「未注册落默认」可构造，同进程差分）。登记 CSharpHost/README。

### D2 源映射从 shLib 字节读（shader）；C# 扫手写源

- **shader**：主线程（EditorService）读 shLib 字节，提取 `m_ShaderFiles` 键与 `ShaderFileInfo.path` 的源路径串（PathHash 序列化即字符串，长度前缀 ASCII），得**实际编译进库的源路径列表**（相对 CAResources，如「Shaders/FinalBlit」）。映射到源文件 `CAResources/<路径>.slang`。**这是真实源映射**（两库各 16 条全有源；缺源分支由夹具覆盖），非扫目录约定。
- **C#**：dll 元数据不存源路径，扫 `Scripting/` 递归 *.cs **排除 obj/ 与生成产物**（git 跟踪的 36 手写源）；**128 生成绑定源（out/build/<preset>/codegen/bindings）本批不显示**，明示略去 + 取回办法（该目录路径）。

### D3 提供者纯投影，IO 在主线程（守 AssetDisplay 契约）

扩 `AssetContext` 增 `SourceFiles`（主线程填好的源文件列表：**源路径 + 存在性**，供提供者定 kind）。**EditorService 主线程**读 shLib 字节（shader）/扫手写源（C#）填 context；**提供者只投影**成 {path, kind} 条目，不碰文件系统、不调门面（守 AssetDisplay.cs 契约）。源文件不存在 → kind=shader-source-missing（不静默跳过，见 D5）。**不算 FileInfo 大小/时间、不算 HasResource**（那些是强加固定字段，D5 删）。

### D4 源条目路径基准：相对工程根 + 扩协议（MODIFIED）

源文件在 CAResources/Scripting（**资源根 CAAssets 之外**），无法相对资源根。源条目 path/nodeId = **相对工程根**（如 `CAResources/Shaders/FinalBlit.slang`、`Scripting/Managed/Host.cs`），kind=shader-source/csharp-source 区别于数据型。**MODIFIED PROTOCOL.md 2.6 + editor-asset-browser dock 要求**：数据型条目仍相对资源根、与真实文件逐条一致；**源条目（kind=shader-source/csharp-source）相对工程根**，由提供者产出。工程根 = env `CASCADED_EDITOR_PROJECT_ROOT`（测试传临时根；默认资源根上级）。三处镜像（PROTOCOL.md/protocol.ts/EditorProtocol.cs）同步改。前端按 kind 区分渲染（数据型/源型不混基准）。

### D5 条目极简：只有资源路径 + 资源类型，提供者全权浏览器忠实渲染（2026-11-25 用户裁定）

**用户明令**（原话「除了资源路径和资源类型，其它几个条目都是你擅自加的东西，和设计会冲突」「这个未注册可不是我想要的东西」「提供者愿意完全可以注册不存在的东西，资源窗口也应该忠实显示」）。原则：**提供者决定显示什么（含不存在/虚拟条目），浏览器忠实渲染；条目骨架只有 path + kind，不强加任何固定元数据字段**。
- **AssetEntry 极简两字段：path（资源路径）+ kind（资源类型）**。删 sizeBytes/modifiedUtc/registered/children/nodeId（nodeId 由 path 充当稳定标识）。PROTOCOL.md 2.6 + protocol.ts + EditorProtocol.cs 三处镜像同步改两字段；前端 assets.ts **通用渲染 path + kind 两列**，删大小/修改时间/已注册列。
- **大小/时间/注册/children 全是主线程算 FileInfo/HasResource 强加的固定字段，与提供者全权冲突，删**。提供者想显示什么内容自己产（缺源用 kind 变体如 shader-source-missing 表达，或路径本身），浏览器不固定表头、不强加元数据。
- 主线程不再为显示算 FileInfo/HasResource 填固定字段；提供者产 {path, kind} 条目浏览器就画。缺源历史路径仍产条目（kind=shader-source-missing，不静默跳过）；源根不可读/非法 → 显式错误。
- 既有 TestEditorAssetBrowser 的固定列断言（七项含 sizeBytes/modifiedUtc/registered）MUST 改成 path+kind 两项；探针夹具其余断言（路径集合/刷新新增/空根）不变。

### D6 枚举整合 + 两 shLib 去重 + 稳定序

`EnumerateAssets`：① CAAssets 文件枚举 → Resolve(kind, relative).Describe（代码型提供者产源条目隐程序集）；② 注册显示资源（C# dll，path 不在 CAAssets）引入；③ **两 shLib 各产源条目，按 path 去重**（缺源 kind 保留）；④ 按 path 序数排序。assets-changed 快照按源条目 path 集合比对。list-assets **两字段形状（path + kind）**。

### D7 验收（可失败 + 视觉 + 回归）

- tester `TestCodeSourceDisplay`（独立进程，进 --list 不进全量；**构建在跑测试前**）：临时工程根预置**两个 shLib**（写死源路径串）+ 对应 .slang 源 + 注册 C# 显示资源 + 手写 .cs（含 obj 干扰文件验排除）→ list-assets 断言：**shLib/dll 条目不出现**、源条目逐条 **path（相对工程根）/kind** 等于写死期望、**源条目数==写死值**、两 shLib 去重后无重复 path、缺源历史路径条目存在（kind=shader-source-missing）；红态：`CASCADED_EDITOR_SOURCE_DISPLAY=off`→shLib 落默认仍显（同进程差分）；obj 生成 .cs 不出现；源根非法→显式错误。断言落点＝**list-assets 结果**（tester C++ 读，无需新 C# 测试工程）。
- 截图视觉（主循环）：**钉死临时夹具**（小、全表可见，CASCADED_EDITOR_ASSET_ROOT + PROJECT_ROOT），dock **两列 path+kind** 显示源条目无 shLib/dll，核「共 N 条」写死读数 + 源条目路径，CapturePreviewAsync 不前置。
- 回归：现有 22 用例 + CoreTests 全绿；**TestEditorAssetBrowser（第 23）单独跑仍过**（探针 Browser.shLib 落默认）；绑定产物逐字节不变；check_csharp_host_readme 0 问题（新 env 登记，注意它扫 Host.cs/UserScript.cs，编辑器 env 在 EditorService.cs 须确认覆盖）。

## Risks / Trade-offs

- [源条目相对工程根扩协议] → D4 MODIFIED PROTOCOL + dock + 视觉三处，前端按 kind 区分；数据型行为不变。
- [shLib 字节读源路径] → 依赖 PathHash 序列化格式（字符串长度前缀）；格式变须同步（写死字节夹具核）。
- [C# 只手写源] → 128 生成绑定明示略去 + 取回目录；obj 排除。
- [两 shLib 去重] → registered 均 false 无歧义；缺源标记保留。

## Migration Plan

纯增量编辑器侧 + 协议加法（源条目基准）。回滚=撤增量 + off 开关；数据型条目行为不变。绑定产物逐字节不变硬判据。

## Open Questions

1. 缺源历史路径（真实库当前 0 条、夹具构造）展示形态（标记字段 vs children）——取条目存在性标记，UI 批细化。
2. 生成绑定源是否后续纳入显示——本批略去 + 取回，后续批定。
3. check_csharp_host_readme 是否扫 EditorService.cs（编辑器 env）——实现时核，不覆盖则补脚本或登记口径。