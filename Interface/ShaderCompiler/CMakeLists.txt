set(PROJECT_NAME ShaderCompiler)
project(${PROJECT_NAME})

file(GLOB_RECURSE Header_List "*.h")
add_library(${PROJECT_NAME} INTERFACE ${Header_List})
target_link_libraries(${PROJECT_NAME} INTERFACE CACore)

target_include_directories(${PROJECT_NAME} INTERFACE header)
