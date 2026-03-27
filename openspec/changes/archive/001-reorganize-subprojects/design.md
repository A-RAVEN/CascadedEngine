# Design: Reorganize Subprojects

**Change ID**: 001-reorganize-subprojects

---

## 目录结构设计

### 变更前
```
CascadedEngine/
├── DotNetHost/
├── CoreTests/
├── D3D12RenderBackendTester/
├── VulkanRendererBackendTester/
├── RenderInterface/
├── ShaderCompiler/
├── IOManager/
├── TimerSystem/
└── ... (其他项目)
```

### 变更后
```
CascadedEngine/
├── Experimental/
│   └── DotNetHost/
├── Test/
│   ├── CoreTests/
│   ├── D3D12RenderBackendTester/
│   └── VulkanRendererBackendTester/
├── Interface/
│   ├── RenderInterface/
│   ├── ShaderCompiler/
│   ├── IOManager/
│   └── TimerSystem/
└── ... (其他项目保持不变)
```

---

## 移动计划

### Experimental 文件夹

| 项目 | 原路径 | 新路径 | 原因 |
|------|--------|--------|------|
| DotNetHost | ./DotNetHost | ./Experimental/DotNetHost | 实验性 .NET 宿主，未启用 |

### Test 文件夹

| 项目 | 原路径 | 新路径 | 原因 |
|------|--------|--------|------|
| CoreTests | ./CoreTests | ./Test/CoreTests | 核心功能测试 |
| D3D12RenderBackendTester | ./D3D12RenderBackendTester | ./Test/D3D12RenderBackendTester | D3D12 后端测试 |
| VulkanRendererBackendTester | ./VulkanRendererBackendTester | ./Test/VulkanRendererBackendTester | Vulkan 后端测试 |

### Interface 文件夹

| 项目 | 原路径 | 新路径 | 原因 |
|------|--------|--------|------|
| RenderInterface | ./RenderInterface | ./Interface/RenderInterface | 渲染抽象接口 |
| ShaderCompiler | ./ShaderCompiler | ./Interface/ShaderCompiler | 着色器编译接口 |
| IOManager | ./IOManager | ./Interface/IOManager | I/O 管理接口 |
| TimerSystem | ./TimerSystem | ./Interface/TimerSystem | 计时器系统接口 |

---

## CMake 更新要点

### 顶层 CMakeLists.txt
```cmake
# 旧路径
add_subdirectory(DotNetHost)
add_subdirectory(CoreTests)
add_subdirectory(RenderInterface)

# 新路径
add_subdirectory(Experimental/DotNetHost)
add_subdirectory(Test/CoreTests)
add_subdirectory(Interface/RenderInterface)
```

### 子项目 CMakeLists.txt
- 更新 `target_include_directories` 中的相对路径
- 更新 `target_link_libraries` 中的依赖路径
- 调整 `PRIVATE/PUBLIC` 路径深度

---

## Git 操作

使用 `git mv` 保留文件历史：
```bash
git mv DotNetHost Experimental/DotNetHost
git mv CoreTests Test/CoreTests
# ... 其他移动
```

---

## 风险与缓解

| 风险 | 缓解措施 |
|------|----------|
| 外部引用失效 | 全局搜索更新所有路径引用 |
| 相对路径错误 | 逐个验证 CMake 配置 |
| IDE 缓存问题 | 清理并重新生成解决方案 |
