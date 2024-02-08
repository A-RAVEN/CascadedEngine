set(PROJECT_NAME CACore)
#project (${PROJECT_NAME})
#file(GLOB_RECURSE Header_List "*.h")
file(GLOB_RECURSE Source_List "*.cpp")

add_library(CACore STATIC ${Source_List})
target_link_libraries(CACore PUBLIC EASTL)
target_include_directories(CACore PUBLIC header)
