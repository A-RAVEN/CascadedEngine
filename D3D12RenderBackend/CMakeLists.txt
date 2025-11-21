set(PROJECT_NAME D3D12RenderBackend)
project(${PROJECT_NAME})

#DMA Only Master Works For Now
CPMAddPackage(
  NAME D3D12MemoryAllocator
  GIT_TAG master
  GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
)

CPMAddPackage(
  NAME DirectXTex
  GIT_TAG mar2025
  GITHUB_REPOSITORY microsoft/DirectXTex
)

CPMAddPackage(
  NAME DirectX-Headers
  VERSION 1.615.0
  GITHUB_REPOSITORY microsoft/DirectX-Headers
  EXCLUDE_FROM_ALL TRUE
)

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})
target_link_libraries(${PROJECT_NAME} PRIVATE d3d12.lib dxgi dxguid d3dcompiler)
target_include_directories(${PROJECT_NAME} PRIVATE "private")
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering)
#target_link_libraries(${PROJECT_NAME} PRIVATE VulkanMemoryAllocator)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw)
target_link_libraries(${PROJECT_NAME} PRIVATE DirectX-Headers)
target_link_libraries(${PROJECT_NAME} PRIVATE D3D12MemoryAllocator)
target_link_libraries(${PROJECT_NAME} PRIVATE CAGeneralReourceSystem_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE DirectXTex)
target_link_libraries(${PROJECT_NAME} PUBLIC IOManager)
CA_SETUP_MODULE(${PROJECT_NAME})
