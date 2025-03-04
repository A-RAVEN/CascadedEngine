set(PROJECT_NAME VulkanRenderBackend)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_include_directories(${PROJECT_NAME} PRIVATE "private")
target_link_libraries(${PROJECT_NAME} PRIVATE VulkanSDK)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE VulkanMemoryAllocator)
target_link_libraries(${PROJECT_NAME} PRIVATE CAGeneralReourceSystem_Interface)

#[[
CPMAddPackage(
  NAME Vulkan-Hpp
  GIT_TAG v1.4.309
  GITHUB_REPOSITORY KhronosGroup/Vulkan-Hpp
)

CPMAddPackage(
  NAME volk
  GIT_TAG 1.4.304
  GITHUB_REPOSITORY zeux/volk
)

if (WIN32)
   set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
endif()
#target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan-Hpp)
target_link_libraries(${PROJECT_NAME} PRIVATE volk_headers)
]]
