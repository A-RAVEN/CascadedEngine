## Context

`GPUBackendTester/Main.cpp` 通过 `std::filesystem::absolute("../../../../")` 定位项目根目录，然后用字符串拼接构造资源和产出路径：

```cpp
ctx.resourcePath = rootPath.string() + "CAResources";  // 缺分隔符
ctx.assetPath   = rootPath.string() + "CAAssets";       // 缺分隔符
```

**两个独立 bug**：

| Bug | 症状 | 触发条件 | 影响 |
|-----|------|---------|------|
| 1. 缺失分隔符 | `E:\Projects\CascadedEngineCAResources` | CWD 深度正确，但 `rootPath.string()` 无尾部分隔符 | 路径粘连 |
| 2. CWD 深度错误 | `E:\CAResources` | CWD 太浅，`../../../../` 解析到盘符根 | 路径指向不存在的位置 |

**当前症状是 Bug 2**：`E:/CAResources`（generic 格式）不存在，`ImportResource` 退出的 log 与此吻合。

`ScanSourceDirectory` 对所有 `ResourceImporterFree`（含 D3D12 和 Vulkan 的 shader importer）传入同样的 sourcePath。D3D12 能工作仅因为其 `D3D12ShaderLibrary.shLib` 在之前路径正确的运行中已序列化到 `CAAssets/`。

**实际构建路径**：`build.py` 中 `BUILD_DIR = out/build/x64-relWithDebugInfo`，DLL 位于 `<project>/out/build/x64-relWithDebugInfo/bin/VulkanRenderBackend.dll`，距项目根 **4** 层：`bin → x64-relWithDebugInfo → build → out → project root`。

## Goals / Non-Goals

**Goals:**
- 修复 Main.cpp 路径拼接缺失分隔符（Bug 1）
- 用 exe 路径推导 + sentinel 文件遍历替代 `../../../../`，根除 CWD 依赖（Bug 2）
- 为 `ShaderImporter_Vulkan` 提供源目录回退机制作为纵深防御
- 移除遗留的 25 个 `[DIAG] fprintf` 调试输出和 `.bak` 文件
- 添加清晰的诊断日志帮助未来快速定位同类问题

**Non-Goals:**
- 不改变 D3D12 后端（其 `D3D12ShaderLibrary.shLib` 已存在，且 D3D12 不是当前开发分支的活跃后端）
- 不改变 `ResourceImportingSystem::ScanSourceDirectory` 的行为
- 不修改 CMake 构建配置
- 不修复 `Test/VulkanRendererBackendTester/private/Main.cpp`（该测试已从 CMakeLists.txt 注释掉，不参与构建；添加 deprecation 注释标记）

## Decisions

### Decision 1: Main.cpp 使用 `operator/` 替代字符串拼接（修 Bug 1）

`std::filesystem::path` 的 `operator/` 自动处理分隔符，且平台无关。**此修复仅处理 Bug 1（缺失分隔符），不修复 Bug 2（CWD 深度）。**

```cpp
// Before (有 bug):
ctx.resourcePath = rootPath.string() + "CAResources";

// After (修复 Bug 1):
ctx.resourcePath = (rootPath / "CAResources").string();
ctx.assetPath    = (rootPath / "CAAssets").string();
```

**重要**：当 CWD 深度错误导致 `rootPath` 解析到 `E:\` 时，`(E:\ / "CAResources").string()` = `"E:\CAResources"` —— 仍然是错误路径。Bug 2 由 Decision 4 解决。

### Decision 2: ShaderImporter_Vulkan 支持显式源目录配置（纵深防御）

当前 `ImportResource` 完全依赖 `ScanSourceDirectory` 传入的 `sourcePath`。如果该路径因 CWD 问题错误，ImportResource 直接退出，没有回退机制。

新增 `SetSourceDirectory()` 方法：

```cpp
class ShaderImporter_Vulkan {
    cafs::path m_SourceDirectory;  // 空路径 = 使用传入的 sourcePath
public:
    void SetSourceDirectory(cafs::path const& path) { m_SourceDirectory = path; }
};
```

`ImportResource` 中：如果 `sourcePath` 不存在且 `m_SourceDirectory` 已设置且存在，使用 `m_SourceDirectory` 作为回退。如果 `m_SourceDirectory` 也未设置，输出 WARNING 日志表明 importer 无回退配置、完全依赖 CWD 路径。

**诊断增强**：
- 当两个路径都失败时，日志 MUST 包含两条路径和 fails 原因
- 当 `sourcePath` 存在但 `m_SourceDirectory` 也设置且路径不同时，输出 INFO 日志
- 当 `sourcePath` 存在且扫描了 0 个 `.slang` 文件时，输出 WARNING

### Decision 3: 从 DLL 路径推导项目根并配置回退源目录

`RenderBackend_Vulkan::Init()` 中配置 `ShaderImporter_Vulkan` 的源目录作为纵深防御。**使用 sentinel 文件遍历，不硬编码固定深度**：

```cpp
// 从 DLL 路径向上遍历，查找包含 CAResources/ 的项目根
fs::path dllPath = getThisModulePath();  // GetModuleHandleEx + GetModuleFileName
for (fs::path p = dllPath.parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path()) {
    if (cafs::exists(p / "CAResources")) {
        m_ShaderImporter.SetSourceDirectory(p / "CAResources");
        CA_LOG("ShaderImporter source directory set to: {}", ...);
        break;
    }
}
```

路径结构（`build.py` 确认）：`<project>/out/build/x64-relWithDebugInfo/bin/VulkanRenderBackend.dll`。

**替代方案**：使用环境变量或 CMake 宏——增加构建系统耦合，且 sentinel 遍历更健壮，可适应不同构建配置（x64-debug、x64-release 等深度变化）。

### Decision 4: Main.cpp 共享项目根推导工具（修 Bug 2）

在 Main.cpp 中实现与 Decision 3 相同策略的项目根推导，使用 **exe 路径**（`GetModuleFileName(NULL, ...)`）而非 DLL 路径。此推导结果替换 `../../../../`，应用于 ALL 路径消费者：

```cpp
fs::path DeriveProjectRoot() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    fs::path p(exePath);
    p = p.parent_path();
    while (!p.empty() && p != p.root_path()) {
        if (fs::exists(p / "CAResources") && fs::exists(p / "CLAUDE.md")) {
            return p;
        }
        p = p.parent_path();
    }
    // Fallback: 使用 CWD-based ../../../../（输出 WARNING）
    CA_LOG_WARN("Project root not found via exe path, falling back to CWD-relative ../../../../");
    return fs::absolute("../../../../");
}
```

## Coverage

本 change 对路径消费者的覆盖范围：

| 消费者 | 保护机制 | 状态 |
|--------|---------|------|
| `ShaderImporter_Vulkan` 源扫描 | Decision 2（回退）+ Decision 3（DLL 推导） | ✅ 完全保护 |
| `ctx.resourcePath` (ScanSourceDirectory) | Decision 4（exe 推导） | ✅ 完全保护 |
| `ctx.assetPath` (SetResourceRootPath) | Decision 4（exe 推导） | ✅ 完全保护 |
| `ctx.editorConfigPath` (IMGUI) | Decision 4（exe 推导） | ✅ 完全保护 |
| `texturFile` (stbi_load) | Decision 4（exe 推导） | ✅ 完全保护 |
| `ctx.resourcePath + "/Images/..."` | Decision 1（operator/）+ Decision 4 | ✅ 完全保护 |
| D3D12 Importer | 无（已有 `.shLib` 缓存） | ⚠️ 依赖缓存 |
| Old VulkanRendererBackendTester | 无（deprecation 注释） | ❌ 不参与构建 |

## Risks / Trade-offs

- **[风险]** Sentinel 文件 `CAResources/` + `CLAUDE.md` 可能在某些 checkout 中不存在 → **缓解**: 回退到 CWD-based `../../../../` + WARNING 日志
- **[风险]** `ShaderImporter_Vulkan::ImportResource` 清除 `ShaderLibrary` 内部 map 无线程同步。当前 ImportResource 在单线程初始化阶段完成，渲染线程在之后启动，因此安全 → **缓解**: 在代码中添加注释："这些 map 非线程安全，ImportResource MUST 仅在单线程初始化阶段调用"
- **[风险]** `ResourceImportingSystem::m_GeneralImporters` 存储裸指针。如果 `RenderBackend_Vulkan` 析构早于 `ResourceImportingSystem`，指针悬空 → **缓解**: 当前二者在 `TestContext` 作用域内同生命周期。记录为已知问题，后续可加 `RemoveImporter`
- **[风险]** D3D12 和 Vulkan 同时加载时 `ScanSourceDirectory` 传入相同 sourcePath 给所有 importer → **缓解**: 当前 Main.cpp 互斥加载后端。如有需求，Decision 2 的 `SetSourceDirectory` 模式可扩展至 D3D12
- **[风险]** `ShaderLibrary` key `"VulkanShaderLibrary.shLib"` 唯一但无命名空间保护 → **缓解**: 已有事实命名规范（`VulkanShaderLibrary.shLib`、`D3D12ShaderLibrary.shLib`、`VKShaderLibrary.shLib`），冲突风险极低
