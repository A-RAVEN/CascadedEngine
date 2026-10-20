set(PROJECT_NAME CoreTests)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_executable(${PROJECT_NAME} ${Header_List} ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE CACore)

target_link_libraries(${PROJECT_NAME} PRIVATE glm)

# `resource_management::TResource`（`GeneralResources/header/CAResource/IResource.h`）与 `ca_io::WBatch` /
# `IOBatch`：8.7 的 `ResourceBatchTest.cpp` 要拿真的 `TResource::Serialize` 与老的批序列化器对拍。
# 只借接口目标（头文件与 IOManager 的声明），不拉那个 MODULE 库。
# 另一个作用：链接它之后 `Test/CoreTests` 的编译环境里就有了 `GeneralResources/header`，生成段解析
# `Tools/CodeGen/TestResource.h` 时才找得到 `CAResource/IResource.h`。
target_link_libraries(${PROJECT_NAME} PRIVATE CAGeneralReourceSystem_Interface)

# ABI-guard compile proof: CoreTests includes the CodeGen tool's demo type
# (Tools/CodeGen/TestEmit.h) + the emitted reflection next to the fixtures' OWNER module. 归属目录决定产物
# 落在哪：夹具的头在 Tools/CodeGen 下，但消费它的编译单元在这个模块里，所以产物跟着这个模块走 ——
# 出仓之后它在构建目录的 `codegen/Test_CoreTests/`，源码树里没有 `generated/` 了。
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Tools/CodeGen ${CMAKE_BINARY_DIR}/codegen/Test_CoreTests ${CMAKE_BINARY_DIR}/codegen/bindings ${CMAKE_SOURCE_DIR}/WindowSystem/header/CAWindow ${CMAKE_SOURCE_DIR}/Interface/RenderInterface/private ${CMAKE_BINARY_DIR}/codegen/Interface_RenderInterface ${CMAKE_SOURCE_DIR}/Interface/RenderInterface/header)

# D3D12 后端的真实类型（`graphics_backend::ShaderLibrary`，在 `D3D12RenderBackend/private` 下）也要有新旧字节
# 对拍：借它的私有头与它的生成物目录，把这个类型编进来。只用到头里的定义与生成的序列化入口，不调用那个
# MODULE 库里的任何函数 —— 所以不 link `D3D12RenderBackend`，只补它头文件需要的那几个目录与库。
# `ShaderLibrary.h` 里 `ComPtr<ID3DBlob>` 的写侧要 `D3DCreateBlob`（在 `d3dcompiler` 里），所以那几个库也要带上。
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/D3D12RenderBackend/private
    ${CMAKE_BINARY_DIR}/codegen/D3D12RenderBackend
    ${CMAKE_SOURCE_DIR}/Interface/ShaderCompiler/header
    ${CMAKE_BINARY_DIR}/codegen/ShaderCompilerSlang
    ${CMAKE_SOURCE_DIR}/WindowSystem/header)
target_compile_definitions(${PROJECT_NAME} PRIVATE D3D12MA_IMPORTS DIRECTX_TEX_IMPORT)
target_link_libraries(${PROJECT_NAME} PRIVATE DirectX-Headers D3D12MemoryAllocator DirectXTex d3d12 dxgi dxguid d3dcompiler)



if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()
