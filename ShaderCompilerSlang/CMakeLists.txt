set(PROJECT_NAME ShaderCompilerSlang)

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

CPMAddPackage(
  NAME slang
  URL https://github.com/shader-slang/slang/releases/download/v2024.17/slang-2024.17-windows-x86_64.zip
  VERSION 2024.17
  # the range-v3 CMakeLists screws with configuration options
  DOWNLOAD_ONLY True
)
find_package(slang REQUIRED PATHS ${slang_SOURCE_DIR}/cmake NO_DEFAULT_PATH)

target_include_directories(${PROJECT_NAME} PRIVATE ${slang_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} PRIVATE ShaderCompiler)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE slang::slang)

add_custom_command(TARGET ShaderCompilerSlang POST_BUILD
  COMMENT "-E copy -t $<TARGET_FILE_DIR:ShaderCompilerSlang> $<TARGET_RUNTIME_DLLS:ShaderCompilerSlang>"
  COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:ShaderCompilerSlang> $<TARGET_RUNTIME_DLLS:ShaderCompilerSlang>
  COMMAND_EXPAND_LISTS
)