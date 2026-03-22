# Data Model: Vulkan Backend RenderInterface Implementation

**Feature**: 002-vulkan-backend-renderinterface | **Date**: 2026-03-22

## Overview

This document defines the entities, their relationships, and state transitions for the VulkanRenderBackendNew implementation.

## Entity Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                     VulkanRenderBackend                          │
│  - vk::Instance m_VulkanInstance                                 │
│  - vk::PhysicalDevice m_PhysicalDevice                          │
│  - vk::Device m_Device                                          │
│  - VmaAllocator m_VmaAllocator                                  │
│  - QueueContext m_QueueContext                                  │
│  - PipelineLibraryCache m_PipelineLibraryCache                  │
└───────────────────────┬─────────────────────────────────────────┘
                        │ owns/manages
        ┌───────────────┼───────────────┬────────────────────────┐
        ▼               ▼               ▼                        ▼
┌───────────────┐ ┌───────────────┐ ┌──────────────────┐ ┌──────────────────┐
│VulkanGPUBuffer│ │VulkanGPUTexture│ │VulkanWindowHandle│ │VulkanShaderStruct│
│               │ │               │ │                  │ │                  │
│- vk::Buffer   │ │- vk::Image    │ │- vk::SurfaceKHR  │ │- DescriptorSet   │
│- VmaAllocation│ │- vk::ImageView│ │- vk::SwapchainKHR│ │  Layouts         │
│- void* mapped │ │- VmaAllocation│ │- VkFormat format │ │- Binding info    │
└───────────────┘ └───────────────┘ └──────────────────┘ └──────────────────┘
        │               │                      │
        │               │                      │
        ▼               ▼                      ▼
┌─────────────────────────────────────────────────────────────────┐
│                    VulkanPipelineLibrary                         │
│  - vk::Pipeline vertexInputLibrary                              │
│  - vk::Pipeline preRasterizationLibrary                         │
│  - vk::Pipeline fragmentLibrary                                 │
│  - vk::Pipeline fragmentOutputLibrary                           │
│  - PipelineCacheKey cacheKey                                    │
└─────────────────────────────────────────────────────────────────┘
```

## Core Entities

### VulkanRenderBackend

**Purpose**: Main backend class implementing CRenderBackend interface

| Field | Type | Description |
|-------|------|-------------|
| m_VulkanInstance | vk::Instance | Vulkan instance |
| m_PhysicalDevice | vk::PhysicalDevice | Selected physical device |
| m_Device | vk::Device | Logical device |
| m_VmaAllocator | VmaAllocator | VMA allocator handle |
| m_QueueContext | QueueContext | Queue management |
| m_DebugMessenger | vk::DebugUtilsMessengerEXT | Debug messenger (debug only) |
| m_PipelineLibraryCache | PipelineLibraryCache | Cached pipeline libraries |
| m_DescriptorSetLayoutManager | DescriptorSetLayoutManager | Descriptor set layout cache |
| m_bPipelineLibrarySupported | bool | VK_EXT_graphics_pipeline_library support |

**Lifecycle**:
- Created: Module initialization via Init()
- Destroyed: Module shutdown via Release()

### VulkanGPUBuffer

**Purpose**: Implements GPUBuffer interface, wraps Vulkan buffer

| Field | Type | Description |
|-------|------|-------------|
| m_Buffer | vk::Buffer | Vulkan buffer handle |
| m_Allocation | VmaAllocation | VMA allocation |
| m_AllocationInfo | VmaAllocationInfo | Allocation metadata |
| m_Size | uint64_t | Buffer size in bytes |
| m_UsageFlags | EBufferUsageFlags | Usage flags from interface |
| m_MappedPtr | void* | Persistently mapped pointer (if applicable) |

**State Transitions**:
```
[Created] → [Ready] → [Mapped] → [Ready] → [Destroyed]
              ↑___________|
```

### VulkanGPUTexture

**Purpose**: Implements GPUTexture interface, wraps Vulkan image

| Field | Type | Description |
|-------|------|-------------|
| m_Image | vk::Image | Vulkan image handle |
| m_ImageView | vk::ImageView | Default image view |
| m_Allocation | VmaAllocation | VMA allocation |
| m_Descriptor | GPUTextureDescriptor | Creation descriptor |
| m_AccessType | ETextureAccessTypeFlags | Access type flags |
| m_CurrentLayout | vk::ImageLayout | Current image layout |

**State Transitions**:
```
[Created] → [Undefined Layout] → [Transfer Dst] → [Shader Read] → [Destroyed]
                                    ↑                  |
                                    |__________________|
```

### VulkanWindowHandle

**Purpose**: Implements WindowHandle interface, manages surface and swapchain

| Field | Type | Description |
|-------|------|-------------|
| m_Surface | vk::SurfaceKHR | Window surface |
| m_Swapchain | vk::SwapchainKHR | Swapchain |
| m_SwapchainImages | vector<vk::Image> | Swapchain images |
| m_SwapchainImageViews | vector<vk::ImageView> | Image views |
| m_Format | vk::Format | Swapchain format |
| m_Extent | vk::Extent2D | Current extent |
| m_CurrentImageIndex | uint32_t | Current acquired image |
| m_Window | shared_ptr<IWindow> | Associated window |

**State Transitions**:
```
[Created] → [Ready] → [Acquired] → [Presented] → [Ready]
                           ↑_______________|
                           |
                     [Outdated] → [Recreated]
```

### VulkanShaderStruct

**Purpose**: Implements ShaderStruct interface, manages descriptor layouts

| Field | Type | Description |
|-------|------|-------------|
| m_DescriptorSetLayouts | vector<vk::DescriptorSetLayout> | Descriptor set layouts |
| m_PipelineLayout | vk::PipelineLayout | Pipeline layout |
| m_Bindings | vector<BindingInfo> | Binding metadata |
| m_StructName | NameHash | Shader struct type name |

### VulkanPipelineLibrary

**Purpose**: Manages cached pipeline library parts

| Field | Type | Description |
|-------|------|-------------|
| m_VertexInputLibrary | vk::Pipeline | Vertex input interface library |
| m_PreRasterizationLibrary | vk::Pipeline | Pre-rasterization shaders library |
| m_FragmentLibrary | vk::Pipeline | Fragment shader library |
| m_FragmentOutputLibrary | vk::Pipeline | Fragment output interface library |
| m_CacheKey | PipelineCacheKey | Hash key for cache lookup |
| m_IsComplete | bool | All parts compiled |

**Library Parts** (from VK_EXT_graphics_pipeline_library):
1. **Vertex Input Interface**: Vertex bindings, attributes, input assembly
2. **Pre-Rasterization Shaders**: Vertex shader, tessellation, geometry
3. **Fragment Shader**: Fragment shader only
4. **Fragment Output Interface**: Color attachments, blend state

### VulkanCommandListManager

**Purpose**: Manages command pools and allocates command buffers (like D3D12 CommandListManager)

| Field | Type | Description |
|-------|------|-------------|
| m_GraphicsPool | vk::CommandPool | Graphics queue command pool |
| m_ComputePool | vk::CommandPool | Compute queue command pool |
| m_TransferPool | vk::CommandPool | Transfer queue command pool |
| m_CommandBuffers | vector<vk::CommandBuffer> | Allocated command buffers |

**Methods**:
- `vk::CommandBuffer& GraphicsCommand()` - Get graphics command buffer
- `vk::CommandBuffer& ComputeCommand()` - Get compute command buffer
- `void Reset()` - Reset all pools for new frame

### VulkanGraphExecutor

**Purpose**: Executes GPUGraph, following D3D12GPUGraphExecutor pattern

| Field | Type | Description |
|-------|------|-------------|
| m_LocalResourceManager | VulkanGraphLocalResourceManager | Graph-local resource allocator |
| m_ConstantBufferManager | VulkanConstantBufferManager | Constant buffer management |
| m_ShaderResourceInstances | ShaderResourceInstanceDic | Shader binding instances |
| m_CurrentFrameContext | PFrameContext | Current frame context |

**Execution States**:
```
[Idle] → [Preparing] → [Executing] → [Presenting] → [Idle]
```

### VulkanGraphLocalResourceManager

**Purpose**: Manages graph-local resources with memory aliasing

| Field | Type | Description |
|-------|------|-------------|
| m_AliasedMemoryAllocator | AliasedMemoryAllocator | Memory aliasing allocator |
| m_ImageResources | map<ImageHandle, VulkanImageResource> | Image resources |
| m_BufferResources | map<BufferHandle, VulkanBufferResource> | Buffer resources |

### VulkanResourceBindingInstance

**Purpose**: Shader resource binding for descriptor sets

| Field | Type | Description |
|-------|------|-------------|
| m_DescriptorSet | vk::DescriptorSet | Vulkan descriptor set |
| m_BindingInfo | BindingInfo | Binding metadata |
| m_ShaderResourceSet | ShaderResourceSet | Associated shader resources |

### VulkanPassRWState

**Purpose**: Track resource read/write states per pass

| Field | Type | Description |
|-------|------|-------------|
| m_ImageRWStates | map<ImageHandle, ResourceState> | Image read/write states |
| m_BufferRWStates | map<BufferHandle, ResourceState> | Buffer read/write states |
| m_QueueTypes | EGPUQueueTypeFlags | Queue types used |

## Relationship Matrix

| From | To | Relationship | Cardinality |
|------|-----|--------------|-------------|
| VulkanRenderBackend | VulkanGPUBuffer | Owns | 1:N |
| VulkanRenderBackend | VulkanGPUTexture | Owns | 1:N |
| VulkanRenderBackend | VulkanWindowHandle | Owns | 1:N |
| VulkanRenderBackend | VulkanShaderStruct | Owns | 1:N |
| VulkanRenderBackend | VulkanPipelineLibrary | Owns (cached) | 1:N |
| VulkanRenderBackend | VulkanCommandListManager | Owns | 1:1 |
| VulkanRenderBackend | VulkanGraphExecutor | Owns | 1:N |
| VulkanWindowHandle | VulkanGPUTexture | References | 1:N (swapchain images) |
| VulkanPipelineLibrary | VulkanShaderStruct | References | N:1 |
| VulkanGraphExecutor | VulkanGraphLocalResourceManager | Owns | 1:1 |
| VulkanGraphExecutor | VulkanConstantBufferManager | Owns | 1:1 |
| VulkanGraphExecutor | VulkanResourceBindingInstance | Owns | 1:N |
| VulkanGraphExecutor | VulkanCommandListManager | Uses | N:1 |
| VulkanGraphLocalResourceManager | VulkanGPUBuffer | Allocates | 1:N |
| VulkanGraphLocalResourceManager | VulkanGPUTexture | Allocates | 1:N |
| VulkanResourceBindingInstance | VulkanShaderStruct | References | N:1 |

## Validation Rules

### Buffer Creation
- Size must be > 0
- Usage flags must include at least one valid usage
- VMA allocation must succeed

### Texture Creation
- Dimensions must be > 0
- Format must be supported by device
- Sample count must be supported for format

### Pipeline Creation
- Valid shader modules required
- Render pass compatible with fragment output
- Vertex input must match shader expectations

### Swapchain Creation
- Surface must be valid
- Format must be supported
- Present mode must be supported

## Memory Aliasing Rules

Resources can share memory when:
1. Lifetimes do not overlap (temporal separation)
2. Memory requirements are compatible (alignment, type)
3. No synchronization hazards between usage

Alias tracking:
- Each resource has a lifetime interval [start_frame, end_frame]
- Memory pool tracks active allocations
- New allocation checks for reusable memory in inactive interval
