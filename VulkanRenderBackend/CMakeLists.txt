set(PROJECT_NAME VulkanRenderBackend)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_link_directories(${PROJECT_NAME} PRIVATE
    "$ENV{VK_SDK_PATH}/Lib"
    "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE "glfw3.lib" "vulkan-1.lib")

target_include_directories(${PROJECT_NAME} PUBLIC "$ENV{VK_SDK_PATH}/Include")
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()