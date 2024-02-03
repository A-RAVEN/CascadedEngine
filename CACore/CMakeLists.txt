set(PROJECT_NAME CACore)
#project (${PROJECT_NAME})
#file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} INTERFACE)
target_link_libraries(${PROJECT_NAME} INTERFACE EASTL)
target_include_directories(${PROJECT_NAME} INTERFACE header)
