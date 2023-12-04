set(PROJECT_NAME VulkanRendererBackendTester)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_executable(${PROJECT_NAME} ${Header_List} ${Source_List} "private/KeyCodes.h")

target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}")

target_link_libraries(${PROJECT_NAME} SDL3::SDL3-static)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()