## ADDED Requirements

### Requirement: ShaderImporter_Vulkan SHALL support explicit source directory fallback

`ShaderImporter_Vulkan` SHALL 支持通过 `SetSourceDirectory()` 显式配置源文件搜索目录。当 `ImportResource` 被调用时，如果传入的 `sourcePath` 不存在且已配置 `m_SourceDirectory` 为有效路径，SHALL 使用 `m_SourceDirectory` 作为回退路径。

#### Scenario: 源目录已存在时使用传入路径
- **WHEN** `ImportResource(resourceManager, sourcePath, destPath)` 被调用且 `cafs::exists(sourcePath)` 为 true
- **THEN** 系统使用 `sourcePath` 作为源目录进行 shader 扫描编译

#### Scenario: 源目录不存在时使用配置的回退路径
- **WHEN** `ImportResource(resourceManager, sourcePath, destPath)` 被调用且 `cafs::exists(sourcePath)` 为 false
- **AND** `m_SourceDirectory` 已被 `SetSourceDirectory()` 设置为有效路径且 `cafs::exists(m_SourceDirectory)` 为 true
- **THEN** 系统使用 `m_SourceDirectory` 作为回退源目录进行 shader 扫描编译
- **AND** 输出 INFO 日志记录 fallback 触发及两条路径

#### Scenario: 所有路径均不存在时跳过并记录日志
- **WHEN** `ImportResource(resourceManager, sourcePath, destPath)` 被调用
- **AND** `sourcePath` 不存在且 `m_SourceDirectory` 未设置或也不存在
- **THEN** 系统输出 WARNING 日志，日志 MUST 包含 sourcePath 和 m_SourceDirectory 两条路径及失败原因
- **AND** 不执行任何编译

#### Scenario: 回退路径未配置时发出明确警告
- **WHEN** `ImportResource(resourceManager, sourcePath, destPath)` 被调用
- **AND** `sourcePath` 不存在且 `m_SourceDirectory` 为空（未调用 SetSourceDirectory）
- **THEN** 系统输出 WARNING 日志明确说明 "no fallback source directory configured, importer depends entirely on ScanSourceDirectory path"
- **AND** 不执行任何编译

#### Scenario: 源目录存在但回退路径不同时记录差异
- **WHEN** `ImportResource(resourceManager, sourcePath, destPath)` 被调用
- **AND** `cafs::exists(sourcePath)` 为 true 且 `!m_SourceDirectory.empty()` 且 `sourcePath != m_SourceDirectory`
- **THEN** 系统使用 `sourcePath` 作为源目录（正常行为）
- **AND** 输出 INFO 日志注明两条路径不同

---

### Requirement: 项目根路径推导 SHALL 使用 sentinel 文件遍历替代 CWD 相对路径

`GPUBackendTester/Main.cpp` 中推导项目根目录 SHALL 使用 exe 路径（`GetModuleFileName(NULL, ...)`）向上遍历父目录，查找 sentinel 文件/目录（`CAResources/` + `CLAUDE.md`），替代脆弱的 `../../../../` CWD 相对路径。

#### Scenario: 通过 sentinel 文件成功找到项目根
- **WHEN** exe 路径为 `<project>/out/build/x64-relWithDebugInfo/bin/GPUBackendTester.exe`
- **AND** 向上遍历在 4 层后找到同时包含 `CAResources/` 和 `CLAUDE.md` 的目录
- **THEN** `rootPath` 被设置为该目录路径（即 `<project>/`）
- **AND** `resourcePath` = `rootPath / "CAResources"`、`assetPath` = `rootPath / "CAAssets"`、`editorConfigPath` = `rootPath / "EditorConfigs"` 均指向正确位置

#### Scenario: Sentinel 文件未找到时回退到 CWD 相对路径
- **WHEN** 向上遍历至文件系统根仍未找到 `CAResources/` + `CLAUDE.md`
- **THEN** 系统输出 WARNING 日志
- **AND** 回退使用 `std::filesystem::absolute("../../../../")` 作为 rootPath

#### Scenario: 不同构建配置（不同深度）下仍正确
- **WHEN** 构建配置从 `x64-relWithDebugInfo` 变为 `x64-debug` 或 `x64-release`（仍为 4 层深度）
- **THEN** sentinel 遍历仍能正确定位项目根（不依赖固定深度）

---

### Requirement: 资源路径拼接 SHALL 使用文件系统路径操作符

`GPUBackendTester/Main.cpp` 中构造 `resourcePath` 和 `assetPath` 时 SHALL 使用 `std::filesystem::path::operator/` 而非字符串拼接，确保平台无关的路径分隔符。

#### Scenario: 路径正确拼接（非盘符根）
- **WHEN** 项目根路径为 `E:\Projects\CascadedEngine` 且子目录为 `CAResources`
- **THEN** `resourcePath` 为 `E:\Projects\CascadedEngine\CAResources`（包含正确的路径分隔符）

#### Scenario: 盘符根路径有尾部分隔符时正确处理
- **WHEN** 项目根路径为 `E:\`（盘符根，自带尾部分隔符）
- **THEN** `resourcePath` 为 `E:\CAResources`（不产生双分隔符）

#### Scenario: operator/ 不修复 CWD 深度 bug
- **WHEN** 项目根路径因 CWD 深度错误解析到 `E:\`（盘符根）且 Decision 4 的 sentinel 遍历未启用
- **THEN** `(E:\ / "CAResources").string()` = `"E:\CAResources"` —— 路径格式正确但内容仍错误
- **AND** Bug 2（CWD 深度）需由 Decision 4 的 sentinel 遍历解决，operator/ 仅解决 Bug 1（分隔符）
