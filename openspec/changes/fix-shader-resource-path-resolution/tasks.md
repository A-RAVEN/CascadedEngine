## 1. Main.cpp 路径拼接修复

- [ ] 1.1 将 `ctx.resourcePath = rootPath.string() + "CAResources"` 改为 `(rootPath / "CAResources").string()`
- [ ] 1.2 将 `ctx.assetPath = rootPath.string() + "CAAssets"` 改为 `(rootPath / "CAAssets").string()`
- [ ] 1.3a 将 `ctx.editorConfigPath = rootPath.string() + "EditorConfigs"` 改为 `(rootPath / "EditorConfigs").string()`（与 1.1/1.2 同模式：root + subdir 无分隔符）
- [ ] 1.3b 审计 `texturFile`（line 341: `ctx.resourcePath + "/Images/test.png"`、line 470: `ctx.resourcePath + "/Images/vulkanlogo.png"`）—— 已自带 "/" 分隔符，改为 `(fs::path(ctx.resourcePath) / "Images" / "test.png").string()` 以统一风格
- [ ] 1.3c 全量审计 Main.cpp 中所有路径拼接：grep `resourcePath`、`assetPath`、`editorConfigPath`、`rootPath`、`texturFile`，逐个确认修复或标记为无需修改

## 2. Main.cpp 共享项目根推导工具（Decision 4）

- [ ] 2.1 实现 `DeriveProjectRoot()` 函数：使用 `GetModuleFileNameW(NULL, ...)` 获取 exe 路径，向上遍历 `parent_path()`，在每层检查 `fs::exists(parent / "CAResources") && fs::exists(parent / "CLAUDE.md")` 作为 sentinel
- [ ] 2.2 Sentinel 遍历失败时回退到 `fs::absolute("../../../../")` 并输出 WARNING 日志
- [ ] 2.3 将 `rootPath = fs::absolute("../../../../")` 替换为 `rootPath = DeriveProjectRoot()`

## 3. ShaderImporter_Vulkan 源目录回退机制

- [ ] 3.1 在 `ShaderImporter_Vulkan.h` 中添加 `cafs::path m_SourceDirectory` 成员和 `void SetSourceDirectory(cafs::path const&)` 方法
- [ ] 3.2 在 `ImportResource` 中实现回退逻辑：
  - 若 `!cafs::exists(sourcePath)` 且 `!m_SourceDirectory.empty()` 且 `cafs::exists(m_SourceDirectory)`，使用 `m_SourceDirectory` 作为源目录
  - 若 `!cafs::exists(sourcePath)` 且 `m_SourceDirectory.empty()`，输出 WARNING："no fallback configured"
  - 若两个路径都不存在，输出 WARNING 包含两条路径
  - 若 `sourcePath` 存在且 `m_SourceDirectory` 非空且路径不同，输出 INFO 注明差异
- [ ] 3.3 `ImportResource` 内部使用 `sourcePath` 的所有位置（`recursive_directory_iterator`、`relative`、`AddInlcudePath`）统一使用最终选定的路径变量

## 4. RenderBackend_Vulkan 初始化配置源目录（Decision 3）

- [ ] 4.1 在 `RenderBackend_Vulkan::Init()` 中，注册 importer 后调用 sentinel 遍历推导项目根并设置回退源目录
- [ ] 4.1a 使用 `GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, ...)` + `GetModuleFileNameW` 获取 DLL 完整路径（通过函数指针地址获取 HMODULE 比 `GetModuleHandleA("VulkanRenderBackend.dll")` 更健壮）
- [ ] 4.1b 从 DLL 路径向上遍历 `parent_path()`，在每层检查 `cafs::exists(parent / "CAResources")`，最多遍历 10 层
- [ ] 4.1c 找到后调用 `m_ShaderImporter.SetSourceDirectory(parent / "CAResources")` 并输出 INFO 日志
- [ ] 4.1d 未找到时输出 WARNING 日志，包含 DLL 路径和最深检查目录

## 5. 清理遗留调试代码

- [ ] 5.1 移除 `VulkanGraphExecutor.cpp` 中 13 个 `[DIAG] fprintf(stderr, ...)` 调用和对应 `fflush(stderr)`
- [ ] 5.2 移除 `VulkanCommandListManager.cpp` 中 12 个 `[DIAG] fprintf(stderr, ...)` 调用和对应 `fflush(stderr)`
- [ ] 5.3 删除 4 个 `.bak` 文件：`VulkanGraphExecutor.cpp.bak`、`.bak2`、`.bak3`、`.bak5`
- [ ] 5.4 在 `ShaderImporter_Vulkan::ImportResource` 和 `ShaderLibrary` 类中添加注释：ShaderLibrary 内部 map 非线程安全，ImportResource MUST 仅在单线程初始化阶段调用

## 6. 旧测试文件标记

- [ ] 6.1 在 `Test/VulkanRendererBackendTester/private/Main.cpp` 顶部添加 `// Deprecated: not built; path concatenation bug (lines 63-65) left unfixed. Use GPUBackendTester instead.`

## 7. 编译验证

- [ ] 7.1 运行 `python build.py` 确认编译通过

## Follow-up（不在本次范围内，记录追踪）

- CMake `CARESOURCES_DIR` 宏：通过 `add_compile_definitions(PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}")` 彻底消除运行时路径推导
- `ResourceImportingSystem::RemoveImporter`：解决裸指针悬空风险
- D3D12 Importer `SetSourceDirectory` fallback：当 D3D12 `.shLib` 缓存被清除后提供同等保护
