# CascadedEngine 项目基线规范

**Version**: 1.0
**Created**: 2026-03-27
**Status**: Baseline

---

## 概述

CascadedEngine 是一个多后端渲染引擎，采用可插拔的图形后端架构。引擎使用 Render Graph (GPUGraph) 架构进行帧编排，支持 D3D12 和 Vulkan 两种渲染后端。

---

## 子项目索引

### 核心基础设施 (Core Infrastructure)

| 子项目 | 类型 | 后端域 | 状态 | 描述 |
|--------|------|--------|------|------|
| CACore | 库 | 通用 | 已启用 | 引擎核心：模块管理、反射、序列化、哈希、日志 |
| ThreadManager | 库 | 通用 | 已启用 | 线程管理接口 |
| TimerSystem | 库 | 通用 | 已启用 | 计时器系统接口 |
| TimerSystem_Impl | 库 | 通用 | 已启用 | 计时器系统实现 |
| GeneralResources | 库 | 通用 | 已启用 | 通用资源系统 |
| ExternalLib | 库 | 通用 | 已启用 | 外部库集成 |

### 渲染接口层 (Render Interface)

| 子项目 | 类型 | 后端域 | 状态 | 描述 |
|--------|------|--------|------|------|
| RenderInterface | 库 | 通用 | 已启用 | 跨后端渲染抽象：CRenderBackend、GPUGraph、GPUFrame |
| ShaderCompiler | 库 | 通用 | 已启用 | 着色器编译接口 |
| ShaderCompilerSlang | 库 | 通用 | 已启用 | Slang 着色器编译器（SPIR-V/DXIL 输出） |
| IOManager | 库 | 通用 | 已启用 | I/O 管理接口 |
| IOManager_FS | 库 | 通用 | 已启用 | 文件系统 I/O 实现 |

### 窗口与输入 (Window & Input)

| 子项目 | 类型 | 后端域 | 状态 | 描述 |
|--------|------|--------|------|------|
| WindowSystem | 库 | 通用 | 已启用 | 窗口系统（GLFW 实现） |
| IMGUIContext | 库 | 通用 | 已启用 | Dear ImGui 集成 |

### D3D12 后端 (D3D12 Backend)

| 子项目 | 类型 | 后端域 | 状态 | 描述 |
|--------|------|--------|------|------|
| D3D12RenderBackend | 库 | D3D12 | 已启用 | D3D12 渲染后端（参考实现） |

### Vulkan 后端 (Vulkan Backend)

| 子项目 | 类型 | 后端域 | 状态 | 描述 |
|--------|------|--------|------|------|
| VulkanRenderBackend | 库 | Vulkan | 未启用 | 旧版 Vulkan 后端（已废弃） |
| VulkanRenderBackendNew | 库 | Vulkan | 开发中 | 新版 Vulkan 后端（正在开发） |

### 测试项目 (Test Projects)

| 子项目 | 类型 | 后端域 | 状态 | 描述 |
|--------|------|--------|------|------|
| CoreTests | 可执行程序 | 通用 | 已启用 | 核心功能测试 |
| D3D12RenderBackendTester | 可执行程序 | D3D12 | 已启用 | D3D12 后端测试 |
| VulkanRendererBackendTester | 可执行程序 | Vulkan | 未启用 | Vulkan 后端测试 |

### 实验性项目 (Experimental)

| 子项目 | 类型 | 后端域 | 状态 | 描述 |
|--------|------|--------|------|------|
| DotNetHost | 可执行程序 | 通用 | 未启用 | .NET 宿主实验 |
| ShaderProcessor | 可执行程序 | 通用 | 未启用 | 着色器处理器工具 |

---

## 核心架构

### 1. 模块系统 (Module System)

**入口**: `CACore/CAModuleManager`

- 动态加载模块 DLL
- 工厂注册与实例创建
- 依赖注入与链接
- 统一生命周期管理

**模块导出宏**:
```cpp
CA_MODULE_INSTANCE(interface_type, implementation_type, instance_name)
```

### 2. 渲染图架构 (Render Graph Architecture)

**入口**: `RenderInterface/GPUGraph`

渲染图用于编排一帧的工作负载：

```
GPUFrame
├── GPUGraph m_RenderGraph
│   ├── RenderPass[]      // 光栅化渲染通道
│   ├── ComputeBatch[]    // 计算批次
│   ├── GPUDataTransfers  // 数据上传
│   └── FinalizePass      // 呈现准备
└── ImageHandle[] m_PresentWindows  // 待呈现窗口
```

**数据流**:
1. 上层构建 `GPUFrame`
2. 通过 `GPUGraph` 声明资源和 Pass
3. `CRenderBackend::ExecuteGraph()` 执行
4. 后端翻译为 GPU 命令并提交

### 3. 后端抽象 (Backend Abstraction)

**入口**: `RenderInterface/CRenderBackend`

核心接口方法：
- `ExecuteGraph()` - 执行渲染图
- `CreateGPUBuffer()` - 创建缓冲区
- `CreateGPUTexture()` - 创建纹理
- `CreateShaderStruct()` - 创建着色器资源布局
- `GetWindowHandle()` - 获取窗口句柄
- `AnyWindowRunning()` - 检查窗口状态

### 4. 资源管理 (Resource Management)

**生命周期策略**:
- **长期资源**: 引用计数 (shared_ptr)
- **Graph 临时资源**: Graph 作用域生命周期
- **内存别名**: 非重叠生命周期的资源共享内存

**GPU 资源句柄**:
```cpp
ResourceHandleKeyData {
    name + uniqueID + threadID
}
```

---

## 目录结构

```
CascadedEngine/
├── CMakeLists.txt              # 顶层构建配置
├── openspec/                   # OpenSpec 规范目录
│   ├── config.yaml
│   ├── specs/
│   └── changes/
├── Interface/                  # 接口模块 (001 重构后)
│   ├── RenderInterface/
│   ├── ShaderCompiler/
│   ├── IOManager/
│   └── TimerSystem/
├── Test/                       # 测试项目 (001 重构后)
│   ├── CoreTests/
│   ├── D3D12RenderBackendTester/
│   └── VulkanRendererBackendTester/
├── Experimental/               # 实验性项目 (001 重构后)
│   └── DotNetHost/
├── CACore/                     # 核心模块
├── D3D12RenderBackend/         # D3D12 后端
├── VulkanRenderBackendNew/     # Vulkan 后端 (新)
├── WindowSystem/               # 窗口系统
├── ShaderCompilerSlang/        # Slang 编译器
├── IMGUIContext/               # ImGui 集成
└── ...                         # 其他模块
```

---

## 技术栈版本

| 组件 | 版本 | 来源 |
|------|------|------|
| CMake | 3.12+ | 构建系统 |
| C++ | C++20 | 语言标准 |
| MSVC | VS2022 | 编译器 |
| Vulkan SDK | 1.2 | 图形 API |
| VulkanMemoryAllocator | v3.1.0 | GPU 内存管理 |
| D3D12MemoryAllocator | master | GPU 内存管理 (D3D12) |
| GLFW | 3.4 | 窗口管理 |
| Slang | - | 着色器编译 |
| fmt | 11.1.3 | 格式化库 |
| mimalloc | v3.0.8 | 内存分配器 |
| magic_enum | 0.9.7 | 枚举反射 |
| DirectXTex | mar2025 | 纹理处理 |

---

## 模块依赖关系

```
┌─────────────────────────────────────────────────────────────┐
│                      Application                            │
├─────────────────────────────────────────────────────────────┤
│                    D3D12RenderBackend                       │
│                    VulkanRenderBackendNew                   │
├──────────────────┬──────────────────┬──────────────────────┤
│  RenderInterface │  ShaderCompiler  │     WindowSystem     │
│                  │     Slang        │                      │
├──────────────────┴──────────────────┴──────────────────────┤
│                         CACore                              │
│  (ModuleManager, Reflection, Serialization, Logging)        │
├─────────────────────────────────────────────────────────────┤
│              ThreadManager / TimerSystem / IO               │
└─────────────────────────────────────────────────────────────┘
```

---

## 编码规范

### 资源管理
- 使用 RAII 模式管理资源生命周期
- GPU 资源使用 VMA (Vulkan) 或 D3D12MA (D3D12) 分配
- 长期资源使用 `shared_ptr` 引用计数

### 日志与调试
- 使用 CACore 日志系统 (`DebugUtils.h`)
- 关键操作记录日志用于调试
- Vulkan 验证层在 Debug 模式启用

### 命名约定
- 接口类以 `I` 前缀 (如 `IWindowSystem`)
- 实现类无前缀 (如 `WindowSystem`)
- Vulkan 封装类以 `Vulkan` 前缀 (如 `VulkanGPUBuffer`)
- D3D12 封装类无特殊前缀

---

## 变更历史

| 变更 | 描述 | 状态 |
|------|------|------|
| 001-reorganize-subprojects | 子项目目录重构 | 已完成 |
| 002-vulkan-backend-renderinterface | Vulkan 后端实现 | 90/94 完成 |

---

## 参考文档

- Vulkan Graphics Pipeline Library: https://docs.vulkan.org/samples/latest/samples/extensions/graphics_pipeline_library/
- VulkanMemoryAllocator: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/
- D3D12RenderBackend: 作为 RenderInterface 实现参考
- VulkanRenderBackend: 作为 Vulkan API 使用参考
