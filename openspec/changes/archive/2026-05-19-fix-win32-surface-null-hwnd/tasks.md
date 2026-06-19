## 1. 修复 CreateSurface() 中的 HWND 获取

- [x] 将 `surfaceInfo.hinstance = GetModuleHandle(nullptr)` 替换为从 `m_Window->GetWindowSystem()->GetSystemNativeHandle()` 获取
- [x] 将 `surfaceInfo.hwnd = nullptr` 替换为从 `m_Window->GetNativeWindowHandle()` 获取
- [x] 添加 Surface 创建失败的错误处理（检查 `m_Surface` + `CA_LOG_ERR`）

## 2. 修复 GetSizeSafe() 窗口尺寸查询

- [x] 将硬编码的 `return uint2{ 800, 600 }` 替换为调用 `m_Window->GetWindowSize()`

## 3. 编译验证与修复

- [x] 运行 `python build.py`，若失败则分析并修复直到 BUILD SUCCESSFUL
