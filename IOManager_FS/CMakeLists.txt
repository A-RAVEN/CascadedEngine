set(PROJECT_NAME IOManager_FS)
project(${PROJECT_NAME})
file(GLOB_RECURSE Header_List "*.h")
file(GLOB_RECURSE Source_List "*.cpp")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE IOManager)
