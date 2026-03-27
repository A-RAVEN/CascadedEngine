# Design: Vulkan Backend RenderInterface Implementation

**Change ID**: 002-vulkan-backend-renderinterface

---

## Architecture Overview

```
VulkanRenderBackendNew/
├── private/
│   ├── RenderBackend_Vulkan.h/.cpp        # 主入口，实现 CRenderBackend
│   ├── Utils/                              # 工具类
│   │   ├── VulkanIncludes.h                # Vulkan 头聚合
│   │   ├── VulkanDebug.h                   # 调试工具
│   │   ├── HashContainer.h                 # 哈希工具
│   │   └── TypeTraits.h                    # 类型转换
│   ├── VulkanObjects/                      # GPU 对象封装
│   │   ├── VulkanBuffer.h/.cpp             # GPUBuffer 实现
│   │   ├── VulkanTexture.h/.cpp            # GPUTexture 实现
│   │   ├── VulkanShaderStruct.h/.cpp       # ShaderStruct 实现
│   │   └── VulkanWindowHandle.h/.cpp       # WindowHandle 实现
│   ├── VulkanQueue/                        # 队列管理
│   │   └── QueueContext.h/.cpp
│   ├── VulkanObjectManaging/               # 对象管理
│   │   ├── DescriptorSetLayoutManager.h/.cpp
│   │   ├── VertexInputStateManager.h/.cpp
│   │   ├── FragmentOutputStateManager.h
│   │   └── PipelineLayoutManager.h/.cpp
│   ├── PipelineStates/                     # Pipeline 状态
│   │   ├── VertexInputStates.h
│   │   ├── FragmentOutputStates.h
│   │   ├── PipelineLayout.h
│   │   └── ShaderModule.h
│   ├── PipelineLibrary/                    # Pipeline Library
│   │   ├── VulkanPipelineLibrary.h/.cpp
│   │   └── PipelineLibraryCache.h/.cpp
│   ├── ShaderLibrary/                      # Shader 导入
│   │   └── ShaderImporter_Vulkan.h/.cpp
│   ├── ResourceManagement/                 # 资源管理
│   │   ├── VulkanMemoryManager.h/.cpp
│   │   ├── VulkanCommandListManager.h/.cpp
│   │   └── VulkanResourceAliasing.h/.cpp
│   └── GPUGraph/                          # Graph 执行
│       ├── VulkanGraphExecutor.h/.cpp
│       ├── VulkanGraphLocalResourceManager.h/.cpp
│       ├── VulkanPassRWState.h/.cpp
│       └── VulkanResourceBindingInstance.h/.cpp
```

---

## Core Components

### 1. VulkanRenderBackend (Main Entry)

实现 `CRenderBackend` 接口：

```cpp
class VulkanRenderBackend : public CRenderBackend {
public:
    // 生命周期
    void Init(IModuleManager*) override;
    void Release() override;

    // 资源创建
    GPUBufferHandle CreateGPUBuffer(...) override;
    GPUTextureHandle CreateGPUTexture(...) override;
    ShaderStructHandle CreateShaderStruct(...) override;

    // 窗口管理
    WindowHandle* GetWindowHandle(IWindow*) override;
    bool AnyWindowRunning() override;

    // Graph 执行
    void ExecuteGraph(ca_vector<GPUFrame>&) override;

private:
    vk::Instance m_Instance;
    vk::PhysicalDevice m_PhysicalDevice;
    vk::Device m_Device;
    VmaAllocator m_Allocator;
    QueueContext m_QueueContext;
    // ...
};
```

### 2. GPU Resources

**VulkanGPUBuffer**:
- 使用 VMA 分配内存
- 支持 persistent mapping (CPU_ACCESS)
- 支持 staging buffer 上传 (GPU_ONLY)

**VulkanGPUTexture**:
- Image + ImageView + VMA Allocation
- Layout transition 支持
- Staging buffer 上传

### 3. Pipeline Library

使用 `VK_EXT_graphics_pipeline_library` 分离编译：

```
Full Pipeline
├── Vertex Input Interface Library    (缓存复用)
├── Pre-Rasterization Shaders Library
├── Fragment Shader Library
└── Fragment Output Interface Library (缓存复用)
```

**缓存策略**:
- 使用 render state 组合的 hash 作为 key
- 共享 vertex input / fragment output 的 pipeline 被复用

### 4. GPUGraph Executor

遵循 D3D12GPUGraphExecutor 模式：

```cpp
class VulkanGraphExecutor {
    void Prepare();                      // 收集资源、shader 绑定、pass 状态
    void BuildDependencyFreeBatchs();    // 依赖分析、批次构建
    void BuildResourceUsageRanges();      // 生命周期追踪
    void AllocateAliasedResources();      // 内存别名分配
    void PrepareBatchResourceBarriers();  // Pipeline barrier 生成
    void BuildPipelineStates();           // 创建 pipeline 对象
    void Execute();                       // 命令录制与提交
};
```

### 5. Shader Import

使用 `ShaderCompilerSlang::IShaderCompilerManager`:

```cpp
// 流程
AquireShaderCompilerShared()
→ BeginCompileTask()
→ AddSourceFile()
→ SetTarget(eSpirV)
→ Compile()
→ GetResults() → result.m_ReflectionData

// 从 reflection data 提取
- m_BindingInfo → descriptor bindings
- m_BindingDataHierarchies → hierarchical bindings
- m_ShaderStructs → shader structs
```

---

## Key Technical Decisions

| 决策 | 选择 | 原因 |
|------|------|------|
| Vulkan 版本 | 1.2 | 平衡特性与兼容性 |
| 内存分配 | VMA | 行业标准，成熟稳定 |
| Shader 编译 | Slang + SPIR-V | 与 D3D12 后端共享编译器 |
| Pipeline 优化 | Graphics Pipeline Library | 减少运行时 hitching |
| 命令缓冲 | 直接返回 vk::CommandBuffer& | 无需额外封装 |

---

## Data Flow

### Initialization Flow
```
Init()
→ CreateInstance (debug utils, surface extensions)
→ PickPhysicalDevice
→ CreateLogicalDevice (graphics, compute queues)
→ InitVMA
→ InitQueueContext
```

### Frame Execution Flow
```
ExecuteGraph(frames)
→ For each GPUFrame:
    → CleanupWindowHandles (resize, close)
    → VulkanGraphExecutor::Prepare()
    → BuildDependencyFreeBatchs()
    → AllocateAliasedResources()
    → BuildPipelineStates()
    → RecordCommandBuffers()
    → Submit to queues
    → Present swapchains
```

---

## Error Handling

| 场景 | 处理 |
|------|------|
| GPU 内存耗尽 | 优雅失败，记录错误日志 |
| 扩展不可用 | 初始化失败，明确错误信息 |
| Pipeline Library 不支持 | 回退到单体 Pipeline 创建 |
| Shader 编译失败 | 返回 null/invalid，记录错误 |
| Swapchain 重建失败 | 重试或报告致命错误 |
