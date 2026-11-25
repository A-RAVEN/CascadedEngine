# 本面未发现 blocking：⑤ 绑定产物逐字节不变（纯 C#、无 CodeGen 标注）

对应攻击面：影响面与静默降级，子面 ⑤「绑定产物逐字节不变声称」。

结论：**本面未发现 blocking**。该声称在本批声明的改动文件范围内结构性成立，且 `check_binding_baseline.py` 是能失败的判据（缺失/内容变化/未登记多余文件三种非零）。下面是核对依据与三条不构成 blocking 的附注。

审查对象：`proposal.md:30`「不动：……GPUTexture/Mesh 绑定面」、`design.md:48`「绑定产物逐字节不变（纯 C# 编辑器侧）」、`design.md:59`「绑定产物逐字节不变硬判据」、`tasks.md:5/26`；对照 `build.py` 的 `CODEGEN_ANNOTATED`、`Scripting/Managed/Scripting.Managed.csproj`、`Tools/CodeGen/check_binding_baseline.py`。

审查快照（SHA-256 前 16 位）：`design.md` `BB8AD764922E5D86`、`tasks.md` `90FD91739A78EC59`。

## 一、改动文件清单里没有绑定面

本批点名的文件（tasks 1.1 / 2.1 / 2.2 / 2.3 / 3.1 / 4.1）：

```text
Scripting/Managed/Editor/AssetDisplay.cs
Scripting/Managed/Editor/DisplayProviders/ShaderSourceDisplayProvider.cs（新增）
Scripting/Managed/Editor/DisplayProviders/CSharpSourceDisplayProvider.cs（新增）
Scripting/Managed/Editor/EditorService.cs
Test/GPUBackendTester/Main.cpp（新增用例）
CSharpHost/README.md（env 表）
docs/testing.md（清单）
openspec/changes/editor-code-source-display/**
```

没有 C++ 头文件、没有 `build.py` 的 `CODEGEN_ANNOTATED` 清单条目、没有 `IResource.h` / `IResourceFactory.h` / 门面头。`build.py:62-63` 的清单注释写明它是 `(header, type, owner directory)` 的**头文件**清单：

```text
CODEGEN_ANNOTATED = [
    # (header, type, owner directory)：归属目录是**真正编译（消费）这个类型**的模块……
```

绑定产物只由这张清单驱动；本批不增删条目，产物不会重新生成出差异。

## 二、新增 C# 文件不需要改工程文件

`Scripting/Managed/Scripting.Managed.csproj` 没有任何手写 `Compile Include` 源码清单（默认 SDK 通配包含全部 `*.cs`），唯一的显式 Compile 项是生成的绑定：

```text
:55 <ItemGroup>
:56   <Compile Include="@(CascadedBindingFile)" />
:57 </ItemGroup>
```

所以新增 `DisplayProviders/*.cs` 不会改变绑定目录内容，也不需要改 csproj；「纯 C# 侧」这一点成立。

## 三、判据本身能失败

`Tools/CodeGen/check_binding_baseline.py:12-18` 的判定口径：

```text
① 基线里列出的文件在产物目录里找不到  ⇒ 缺失；
② 找到但 SHA-256 与基线不同          ⇒ 内容变了；
③ 产物目录里有、基线里没有、也不在「允许新增」清单里 ⇒ 多余。
```

退出码 0 / 1 / 2 三态，且 `--self-test` 会删一条基线再算一遍防止「脚本永远绿」。若有人在本批里顺手动了一个被 CodeGen 标注的头，它会红。因此「逐字节不变」不是恒真断言。

## 四、附注（不构成 blocking）

1. 对本批实际改动面而言，这条判据**注定为绿**——它只能防「越界改绑定头」，不能证明本批代码没有副作用。这是判据强度问题，不是本批错误。
2. 「纯 C# 编辑器侧」的措辞不精确：验收本身要改 `Main.cpp`（C++）与 `CSharpHost/README.md`、`docs/testing.md`。三处都不在绑定面，不影响结论；但工件里「纯 C#」这句要么改成「不改绑定面」，要么把这三处列进影响面。
3. `check_binding_baseline.py` 依赖 `out/build/<预设>/codegen/bindings`（默认 Debug 预设），任务 4.1 在 4.2 的最终构建之前跑它；因为本批不动 C++，用的是哪次构建的产物不影响哈希（见主张-影响-009 对构建顺序的独立意见）。
