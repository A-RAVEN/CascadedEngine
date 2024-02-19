set(PROJECT_NAME ShaderCompilerSlang)

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE ShaderCompiler)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE slang)
#target_include_directories(${PROJECT_NAME} PRIVATE header)