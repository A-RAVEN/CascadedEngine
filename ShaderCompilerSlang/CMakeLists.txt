set(PROJECT_NAME ShaderCompilerSlang)
set(PROJECT_INTERFACE_NAME ${PROJECT_NAME}_Interface)

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE slang)
target_include_directories(${PROJECT_NAME} PRIVATE header)

add_library(${PROJECT_INTERFACE_NAME} INTERFACE)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE CACore)
target_include_directories(${PROJECT_INTERFACE_NAME} INTERFACE header)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()
