#set(PROJECT_NAME CASTL)
#file(GLOB_RECURSE Header_List "*.h")
#
#add_library(${PROJECT_NAME} INTERFACE ${Header_List})
#target_include_directories(${PROJECT_NAME} INTERFACE header)
#target_compile_definitions(${PROJECT_NAME} INTERFACE USING_EASTL=0)