set(PROJECT_NAME VulkanRendererBackendTester)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

file(GLOB_RECURSE flatbuffers_schemas "*.fbs")

include(${CMAKE_SOURCE_DIR}/CMake/ExternalProjects.cmake)
link_imgui(Source_List)

add_executable(${PROJECT_NAME} ${Header_List} ${Source_List} "private/KeyCodes.h")

target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}")


target_link_libraries(${PROJECT_NAME} PRIVATE "assimp")
#[[
target_link_libraries(${PROJECT_NAME} PRIVATE flatbuffers)
message("   flatbuffers_schemas: ${flatbuffers_schemas}")
build_flatbuffers("${flatbuffers_schemas}"
                  ""
                  flatbuffer_targets
                           ""
                           "${CMAKE_CURRENT_SOURCE_DIR}/private"#generated_includes_dir
                           ""
                           "")

add_dependencies(${PROJECT_NAME} flatbuffer_targets)
]]

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()