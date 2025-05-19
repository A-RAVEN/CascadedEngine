set(PROJECT_NAME VulkanRenderBackend)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

CPMAddPackage(
  NAME VulkanMemoryAllocator
  GIT_TAG v3.1.0
  GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
)

target_include_directories(${PROJECT_NAME} PRIVATE "private")
target_link_libraries(${PROJECT_NAME} PRIVATE VulkanSDK)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering)
target_link_libraries(${PROJECT_NAME} PRIVATE VulkanMemoryAllocator)
target_link_libraries(${PROJECT_NAME} PRIVATE ShaderCompiler)
target_link_libraries(${PROJECT_NAME} PRIVATE CAGeneralReourceSystem_Interface)
