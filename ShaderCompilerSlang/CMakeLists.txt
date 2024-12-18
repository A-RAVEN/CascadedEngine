set(PROJECT_NAME ShaderCompilerSlang)

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE ShaderCompiler)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE slang)

add_custom_command(TARGET ShaderCompilerSlang POST_BUILD
  COMMENT "-E copy -t $<TARGET_FILE_DIR:ShaderCompilerSlang> $<TARGET_RUNTIME_DLLS:ShaderCompilerSlang>"
  COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:ShaderCompilerSlang> $<TARGET_RUNTIME_DLLS:ShaderCompilerSlang>
  COMMAND_EXPAND_LISTS
)