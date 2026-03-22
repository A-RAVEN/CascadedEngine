# Quickstart: Vulkan Backend RenderInterface Implementation

**Feature**: 002-vulkan-backend-renderinterface | **Date**: 2026-03-22

## Prerequisites

1. Vulkan SDK 1.2+ installed
2. VulkanMemoryAllocator integrated
3. ShaderCompilerSlang available
4. Visual Studio 2022 or compatible MSVC toolchain
5. CMake 3.12+

## Quick Validation Checklist

### 1. Build Verification

```bash
# Configure with CMake preset
cmake --preset=x64-relWithDebugInfo

# Build the project
cmake --build out/build/x64-relWithDebugInfo --target VulkanRenderBackendNew
```

**Expected**: VulkanRenderBackendNew.dll compiles without errors

### 2. Backend Initialization Test

```cpp
// Create backend instance
auto backend = CreateRenderBackend_Vulkan();

// Verify initialization
assert(backend != nullptr);
assert(backend->AnyWindowRunning() == false);
```

**Expected**: Backend initializes without crashes or validation errors

### 3. Buffer Creation Test

```cpp
// Create a simple vertex buffer
auto buffer = backend->CreateGPUBuffer(
    GPUBufferDescriptor::Create(1024, sizeof(Vertex)),  // 1KB, Vertex stride
    EBufferUsageFlags::VertexBuffer | EBufferUsageFlags::TransferDst
);

assert(buffer != nullptr);
assert(buffer->GetSize() == 1024);
```

**Expected**: Buffer created with valid handle

### 4. Texture Creation Test

```cpp
// Create a 2D texture
GPUTextureDescriptor texDesc;
texDesc.dimension = ETextureDimension::Texture2D;
texDesc.width = 256;
texDesc.height = 256;
texDesc.format = ETextureFormat::RGBA8;
texDesc.mipLevels = 1;

auto texture = backend->CreateGPUTexture(texDesc, ETextureAccessTypeFlags::ShaderResource);

assert(texture != nullptr);
```

**Expected**: Texture created with valid handle

### 5. Window and Swapchain Test

```cpp
// Create a window
auto window = windowSystem->CreateWindow(800, 600, "Vulkan Test");

// Get window handle for rendering
auto windowHandle = backend->GetWindowHandle(window);

assert(windowHandle != nullptr);
assert(backend->AnyWindowRunning() == true);
```

**Expected**: Window surface and swapchain created

### 6. Triangle Rendering Test (MVP)

```cpp
// Create shader struct
auto shaderStruct = backend->CreateShaderStruct(VertexTypeHash);

// Create pipeline (using graphics pipeline library if available)
auto pipeline = CreatePipeline(shaderStruct, vertexShader, fragmentShader);

// Execute simple render graph
auto graph = CreateSimpleTriangleGraph(pipeline, vertexBuffer);
backend->ExecuteGraph(scheduler, graph);

// Present
windowHandle->Present();
```

**Expected**: Triangle rendered to window

### 7. Pipeline Library Performance Test

```cpp
// Measure monolithic pipeline creation
auto startMonolithic = Now();
CreateMonolithicPipeline(state1);
auto endMonolithic = Now();

// Measure library-based pipeline creation (first time)
auto startLibrary1 = Now();
CreatePipelineWithLibrary(state1);
auto endLibrary1 = Now();

// Measure library-based pipeline creation (reuse)
auto startLibrary2 = Now();
CreatePipelineWithLibrary(state2);  // Shares vertex input with state1
auto endLibrary2 = Now();

// Verify speedup
auto speedup = (endMonolithic - startMonolithic) / (endLibrary2 - startLibrary2);
assert(speedup >= 1.3);  // At least 30% faster
```

**Expected**: Library-based creation 30%+ faster for shared state

## Integration Test Scenario

### Complete Triangle Demo

1. Initialize VulkanRenderBackendNew
2. Create window (800x600)
3. Create vertex buffer with triangle data
4. Compile vertex and fragment shaders
5. Create pipeline with shader struct
6. Create swapchain render targets
7. Render frame with triangle
8. Present to screen
9. Verify triangle is visible (screenshot comparison)

## Debug Checklist

If issues occur:

| Issue | Check |
|-------|-------|
| Backend fails to initialize | Vulkan SDK installed, driver supports Vulkan 1.2 |
| Validation errors | Enable debug messenger, check error messages |
| Buffer creation fails | VMA allocator initialized, sufficient GPU memory |
| Texture creation fails | Format supported, dimensions valid |
| Swapchain creation fails | Surface valid, present mode supported |
| Pipeline creation fails | Shaders compile to SPIR-V, descriptor layouts valid |
| Nothing renders | Pipeline bound, vertex buffer bound, viewport set |

## Performance Validation

Run VulkanRendererBackendTester and verify:

- [ ] Frame time < 16.67ms (60+ fps) for simple scene
- [ ] Pipeline creation < 5ms for complex pipelines
- [ ] Memory usage stable (no leaks after 1000 frames)
- [ ] No validation errors during normal operation

## Next Steps

After quickstart validation passes:

1. Run full VulkanRendererBackendTester suite
2. Compare performance with D3D12RenderBackend
3. Test on multiple hardware configurations
4. Verify graphics pipeline library extension behavior
