# Tasks: editor-code-source-display

> 验收口径：每条给「命令 / 产物 + 期望读数」，期望写死、不从被测物推导；红态存 evidence/。
> 构建唯一路径 python build.py --config Debug，**构建先于跑测试**。编辑器类 MUST 截图视觉验收（CapturePreviewAsync 不前置，AGENTS §2）。
> 纯 C# 编辑器侧 + 协议加法，不动绑定面（绑定产物逐字节不变硬判据）。临时文件 %TEMP% 用完即清，证据只进 change evidence/。
> 范围纪律：「隐程序集显源」只代码型；贴图/模型不预设（用户明令）。

## 1. 显示提供者框架扩展

- [x] 1.1 注册键扩类型+路径 + 生产注册点 + off 开关 `Scripting/Managed/Editor/AssetDisplay.cs`：增 `Register(kind, path, provider)`（path 空＝种类默认，兼容单参）+ `Resolve(kind, path)`（精确→种类默认→默认）；**生产注册 (shader-library,"VulkanShaderLibrary.shLib")/(shader-library,"D3D12ShaderLibrary.shLib")**；off 开关 `CASCADED_EDITOR_SOURCE_DISPLAY=off`（默认 on，off 不注册源显示）。验收：构建过；精确绑定优先（list-assets 夹具可观察）；探针 Browser.shLib 落默认仍显；off 时 shLib 落默认仍显（同进程差分）；grep IResource.h/门面头零改动。
- [x] 1.2 注册显示资源表 + AssetContext.SourceFiles：`RegisterDisplayResource(kind, path, provider)`；扩 `AssetContext` 增 `SourceFiles`（主线程填好的源路径/大小/修改时间/存在性列表）。验收：构建过；登记表可查（list-assets 引入条目可观察）；SourceFiles 由主线程填、提供者只读。

## 2. 代码型提供者与枚举整合

- [x] 2.1 `DisplayProviders/ShaderSourceDisplayProvider.cs`（kind=shader-library）：**主线程（EditorService）读 shLib 字节提取源路径串**（PathHash 序列化字符串，m_ShaderFiles 键 + ShaderFileInfo.path，相对 CAResources）映射 CAResources/<路径>.slang 填 SourceFiles；提供者**纯投影**（不碰文件系统/不调门面）产源条目（kind=shader-source，path/nodeId=相对工程根，registered=HasResource(源 path)=false），**不产 shLib 条目**；缺源历史路径仍产条目（sizeBytes=0+存在性标记，不静默跳过）；两 shLib 按源路径 nodeId 去重。验收：构建过；写死源路径串 shLib 夹具→源条目==写死期望（数写死）；shLib 隐去；缺源标记；去重无重复；grep 提供者文件 Directory/FileInfo/门面 0（evidence/2.1-*.txt）。
- [x] 2.2 `DisplayProviders/CSharpSourceDisplayProvider.cs`（kind=csharp-script）：注册 (csharp-script,"Scripting.Managed.dll")；主线程扫 Scripting/ 手写 *.cs（**排除 obj/生成产物**）填 SourceFiles；提供者纯投影产源条目（kind=csharp-source，registered=false），不产 dll 条目；**128 生成绑定源（out/build/<preset>/codegen/bindings）明示略去+取回目录**。验收：构建过；手写 .cs 夹具（含 obj 干扰）→源条目==写死期望、obj 不出现；dll 隐去；生成绑定略去标记+取回路径（evidence/2.2-*.txt）。
- [x] 2.3 `EditorService.cs` EnumerateAssets 整合 + **PROTOCOL 三处镜像改源条目基准**：① 文件枚举改 Resolve(kind, relative)；② 注册显示资源引入；③ nodeId 去重+path 序数排序；**先改 PROTOCOL.md 2.6（源种类 shader-source/csharp-source 路径相对工程根，数据型相对资源根）再镜像 protocol.ts/EditorProtocol.cs**；前端 assets.ts 按 kind 区分渲染；工程根 env CASCADED_EDITOR_PROJECT_ROOT + 源根 env；**新 env 登记 CSharpHost/README**（核 check_csharp_host_readme 是否扫 EditorService.cs）。验收：构建过；list-assets 源条目相对工程根/数据型相对资源根；三处镜像逐字一致；七字段形状不变；check_csharp_host_readme 0 问题。

## 3. 用例与视觉验收

- [x] 3.1 tester `TestCodeSourceDisplay`（Main.cpp + --list，**独立进程，进 --list 不进全量；构建先于跑**）：临时工程根预置**两个写死源路径串 shLib** + 对应 .slang + 注册 C# 显示资源 + 手写 .cs（含 obj 干扰）→ list-assets 断言 **shLib/dll 不出现** + 源条目逐条 path（相对工程根）/kind/sizeBytes/registered=false==写死期望 + **源条目数==写死值** + 两 shLib 去重无重复 nodeId + 缺源历史路径条目存在且标记 + obj 不出现；红态：`CASCADED_EDITOR_SOURCE_DISPLAY=off`→shLib 落默认仍显（同进程差分）；源根非法→显式错误（不静默空）。断言落点＝list-assets 结果（tester C++ 读）。验收：--list 含 TestCodeSourceDisplay（26）、全量仍 22；独立进程过；红态各能红（evidence/3.1-*.txt）。
- [x] 3.2 截图视觉验收（主循环）：**钉死临时夹具**（CASCADED_EDITOR_ASSET_ROOT + PROJECT_ROOT，小表全可见）交互打开资产浏览器，CapturePreviewAsync 抓窗（不前置）+ read_image 核对 dock 显源条目（kind=shader-source/csharp-source）、**无 shLib/dll**、「共 N 条」==写死值、源条目路径。验收：evidence/3.2-截图.png + read_image 核对记录；抓不到/不符判不通过。

## 4. 回归与编译

- [x] 4.1 回归零差异（主循环跑）：现有 22 用例同进程两后端 name+status 与基线（归档 2026-11-25-source-importers-png-obj/evidence/4.1-全量-*.json）零差异；**TestEditorAssetBrowser 独立跑仍过**（探针 Browser.shLib 落默认）；TestCodeSourceDisplay 独立过；CoreTests exit 0 + PASSED 26（evidence/4.1-CoreTests.txt）；绑定基线 Tools/CodeGen/check_binding_baseline.py --baseline <新锚> --product-dir out/build/x64-debug/codegen/bindings 0/0/0（evidence/4.1-基线.txt）；check_csharp_host_readme 0 问题；--list/docs/testing.md 同步。验收：零差异 + TestEditorAssetBrowser 过 + CoreTests 26 + 基线 0/0/0 + README 0 问题。
- [x] 4.2 编译验证（最后）：python build.py --config Debug 失败修到成功；成功后复跑 4.1 读数不变。验收：BUILD SUCCESSFUL + 复跑一致（evidence/4.2-构建.txt）。

## 5. 条目极简 path+kind（2026-11-25 用户裁定：没要求过的字段都是幻想，删）

> 用户明令「除了资源路径和资源类型，其它几个条目都是你擅自加的东西」「如果我没有要求过，那就是你的幻想」。前 1–4 节实现含 sizeBytes/modifiedUtc/registered/children/nodeId 固定字段，全是用户没要求的幻想，本节删到只剩 path+kind。改完复跑测试 + 重抓 3.2 视觉（两列）。

- [x] 5.1 AssetEntry 极简两字段 + 协议三处镜像：`AssetDisplay.cs` AssetEntry 删到**只剩 path + kind**（删 sizeBytes/modifiedUtc/registered/children/nodeId，path 充当稳定标识）；**先改 PROTOCOL.md 2.6 再镜像 protocol.ts/EditorProtocol.cs**（两字段，版本仍 1）。验收：构建过；grep AssetEntry/PROTOCOL/protocol.ts/EditorProtocol.cs sizeBytes|modifiedUtc|registered|children|nodeId 0；两字段三处逐字一致。
- [x] 5.2 前端通用渲染两列：`EditorWeb/src/assets.ts` 渲染**两列（路径/种类）**，删大小/修改时间/已注册列与文案，按 kind 区分路径基准。验收：tsc+vite 过；grep assets.ts sizeBytes|modifiedUtc|registered|已注册|未注册|大小|修改时间 0。
- [x] 5.3 主线程/提供者去固定元数据：EditorService 不再为显示算 FileInfo（大小/时间）/HasResource；AssetContext.SourceFiles 只剩源路径+存在性；两提供者投影只产 {path, kind}（缺源 kind=shader-source-missing）。验收：构建过；grep 提供者/EditorService 显示路径 sizeBytes|modifiedUtc|registered|FileInfo 0（FileInfo 若仅枚举存在性须点名非显示元数据）。
- [x] 5.4 既有用例改两字段断言：`TestEditorAssetBrowser` 固定列断言改 path+kind 两项（删 sizeBytes/modifiedUtc/registered，探针夹具路径集合/刷新新增/空根不变）；`TestCodeSourceDisplay` 源条目断言改 path+kind（缺源 kind=shader-source-missing）。验收：构建过；两独立用例仍 pass；全量 22 零差异。
- [x] 5.5 截图视觉复核（主循环）：重抓 3.2（**两列 path+kind**），read_image 核对 dock 两列 + 源条目 + 无大小/时间/注册列 + 无 shLib/dll。验收：evidence/3.2-截图.png 覆盖 + 核对记录更新；仍有多余列判不通过。

## 修订记录（对抗验证首轮，五面约 45 条主张，主循环复核按根因归并修订）

> 五面：验收 15（5B）/一致 6（4B）/溯源 11（4B）/影响 9（5B）/存在 4（1B）。按根因归并：

1. **源映射假前提**（溯源-004）：首轮称「PathHash 只存哈希、C# 拿不到源路径串」故扫源目录约定——**错**。PathHash 含 castl::string m_Name（HashKeys.h），序列化写 obj.string()，shLib 字节直接含源路径串（「Shaders/FinalBlit」）。
   - 改后：D2/D3/spec 要求2 **源映射从 shLib 字节读实际编译源路径**（两库各 16 条全有源；缺源分支夹具覆盖），非扫目录约定。**实现期实测纠正**：溯源面首轮「Vulkan 21 条含 5 无源」是裸子串搜索误计 .hslang 包含文件，按 m_ShaderFiles 条数提取实为 16/16 全有源（evidence/2.1-源映射与纯投影.txt）。
2. **源条目路径基准冲突**（验收-001/溯源-008/影响-003/一致-002）：源文件在资源根外，首轮「相对工程根」与 PROTOCOL/生效规范「相对资源根」互斥，无改协议任务。
   - 改后：D4/spec 要求4 + MODIFIED dock/协议/视觉 **源种类相对工程根、数据型相对资源根，先改 PROTOCOL.md 再三处镜像，前端按 kind 区分**；tasks 2.3 加改协议任务。
3. **提供者扫盘违约**（影响-006）：首轮提供者在 Describe 扫源目录，违 AssetDisplay「提供者不碰文件系统」契约。
   - 改后：D3/spec 要求3 **IO 主线程（EditorService 读 shLib 字节/扫源）填 AssetContext.SourceFiles，提供者纯投影**；grep 提供者文件系统/门面 0。
4. **registered 语义违约**（溯源-007/验收-004）：首轮「registered=true 当程序集存在」违 PROTOCOL registered=HasResource。
   - 改后：D5/spec **源条目 registered=HasResource(源 path)=false**（源非注册资产），删「程序集存在」口径。
5. **两 shLib 去重歧义**（影响-005/验收-006）：真实根两 shLib 扫同源根产重复 nodeId，registered 归属不定。
   - 改后：D6/spec 要求2 **源映射从各自 shLib 字节、按源路径 nodeId 去重、registered 均 false 无歧义、缺源标记保留**；夹具预置两 shLib。
6. **静默降级**（溯源-010/影响-006）：隐程序集+源根空 ok=true 空列表无标记无取回。
   - 改后：D5/spec **缺源历史路径仍产条目+存在性标记（不静默跳过）；源根非法显式错误（不空列表冒充）；生成绑定源明示略去+取回目录**。
7. **MODIFIED 与存量互斥**（验收-001/013/影响-002/一致-001/004）：首轮只改显示提供者一条，与 dock「逐条一致/相对资源根/新增+1」、视觉「含真实 shLib」互斥。
   - 改后：MODIFIED **扩到 dock/协议/显示提供者/视觉四条**；逐条一致/新增+1 限定数据型；视觉夹具改探针 Browser.shLib（落默认仍显），源显示视觉归新能力 TestCodeSourceDisplay。
8. **注册键/红态不可构造**（影响-001/验收-007）：shader 注册点未钉死会破 TestEditorAssetBrowser；无注销/开关红态不可构造。
   - 改后：D1/spec 要求1 **生产注册钉死真实 shLib 路径（探针 Browser.shLib 路径不同落默认仍显）+ off 开关 CASCADED_EDITOR_SOURCE_DISPLAY=off**（红态同进程差分）。
9. **dll 源含生成绑定 + obj**（存在-001/溯源-002/验收-005）：dll 由 36 手写+128 生成绑定编来；Scripting 递归含 2 obj 产物。
   - 改后：D2/spec 要求3 **C# 只手写源（排除 obj），128 生成绑定明示略去+取回目录**；红态 obj 不出现。
10. **测试落点/恒真/视觉夹具**（验收-008/002/003/014/015/影响-008/009/溯源-011）：tasks 1.1/1.2 无 C# 测试落点；dll 隐去恒真；源条目数==源文件数恒等式；视觉夹具未钉；4.1 在 4.2 构建前跑旧二进制。
   - 改后：断言落点＝**list-assets 结果（tester C++ 读，无需新 C# 工程）**；源条目数**写死值**；视觉**钉死临时夹具+「共 N 条」写死**；**构建先于跑测试**；--list 26 写死。
11. **数据型不预设自相矛盾**（一致-003/影响-007）：首轮场景给 texture/mesh 定「必须默认」行为，与「不预设」矛盾。
   - 改后：spec 要求1 场景改 **「本能力未注册改变数据型行为的提供者」（可观察：注册种类不含 texture/mesh 行为改变）**，不写数据型展示行为要求。
12. **扫描根/类型名/行号锚**（存在-002/003/004/溯源-006）：ShaderImporter 扫描根 CAResources 全树非 Shaders；Vulkan 是 VulkanShaderFileInfo；行号锚禁。
   - 改后：design Context 改 **CAResources 全树 + 两后端值类型点名（ShaderFileInfo/VulkanShaderFileInfo）+ 逐字引文代行号**；源映射从 shLib 字节（不依赖扫描根约定）。
13. **用户裁定条目极简 path+kind（2026-11-25，原话「除了资源路径和资源类型，其它几个条目都是你擅自加的东西，和设计会冲突」「代码里只有我要求的设计和你幻想的设计，如果我没有要求过那就是你的幻想」「这个未注册可不是我想要的东西」）**：前 1–4 节实现含 sizeBytes/modifiedUtc/registered/children/nodeId 固定字段，全是用户没要求、AI 擅自加的幻想，与「提供者全权浏览器忠实渲染」冲突。主循环一度辩称「只是字段约定/代码里真有」被用户指循环论证（拿自己写的代码给自己设计背书）+前后矛盾——确属幻想未完成。
   - 改前：AssetEntry 七字段（nodeId/path/kind/sizeBytes/modifiedUtc/registered/children）；dock 五列；spec registered=HasResource；TestEditorAssetBrowser 七项断言。
   - 改后：design **D5 改「条目极简 path+kind，提供者全权浏览器忠实渲染」**；AssetEntry **只剩 path+kind 两字段**（删 sizeBytes/modifiedUtc/registered/children/nodeId，path 充当标识）；PROTOCOL 三处镜像两字段；前端**两列通用渲染**；主线程不算 FileInfo/HasResource 显示元数据；缺源用 kind=shader-source-missing；TestEditorAssetBrowser/TestCodeSourceDisplay 改两字段断言；新增 **tasks §5（5.1–5.5）**。审计标准（用户钉死）：**没要求过的字段=幻想，删**。依据：用户当场明令。