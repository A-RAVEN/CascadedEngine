set(PROJECT_NAME ShaderCompiler)
project(${PROJECT_NAME})

file(GLOB_RECURSE Header_List "*.h")
add_library(${PROJECT_NAME} INTERFACE ${Header_List})
target_link_libraries(${PROJECT_NAME} INTERFACE CACore)

target_include_directories(${PROJECT_NAME} INTERFACE header)

# CodeGen 生成物：这些类型的编译环境来自 ShaderCompilerSlang 的编译单元，所以生成物落在那个归属目录下
# —— 出仓之后它在**构建目录**里（`codegen/ShaderCompilerSlang`），源码树里不再有 `generated/`。
# 这个头文件被两个渲染后端消费，生成目录必须跟着这个接口目标一起导出。
target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_BINARY_DIR}/codegen/ShaderCompilerSlang)
