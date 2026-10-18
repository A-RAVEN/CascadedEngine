set(PROJECT_NAME ScriptHost)
project(${PROJECT_NAME})
file(GLOB_RECURSE Header_List "*.h")

# 纯头文件接口：没有 .cpp，所以做成 INTERFACE 目标（不产出 .lib，只带 include 目录与依赖）
add_library(${PROJECT_NAME} INTERFACE ${Header_List})
target_include_directories(${PROJECT_NAME} INTERFACE header)
target_link_libraries(${PROJECT_NAME} INTERFACE CACore)
