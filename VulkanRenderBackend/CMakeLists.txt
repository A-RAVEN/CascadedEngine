set(PROJECT_NAME VulkanRenderBackend)

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_link_directories(${PROJECT_NAME} PRIVATE
    "$ENV{VK_SDK_PATH}/Lib"
)

target_include_directories(${PROJECT_NAME} PUBLIC "$ENV{VK_SDK_PATH}/Include")
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()