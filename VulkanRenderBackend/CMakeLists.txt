set(PROJECT_NAME VulkanRenderBackend)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_include_directories(${PROJECT_NAME} PRIVATE "$ENV{VK_SDK_PATH}/Include")
target_link_directories(${PROJECT_NAME} PRIVATE
    "$ENV{VK_SDK_PATH}/Lib"
    "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
)
target_link_libraries(${PROJECT_NAME} PRIVATE "vulkan-1.lib")
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE VulkanMemoryAllocator)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw)
