set(PROJECT_NAME Rendering)
set(PROJECT_INTERFACE_NAME ${PROJECT_NAME}_Interface)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE ShaderCompiler)
target_include_directories(${PROJECT_NAME} PRIVATE header)

add_library(${PROJECT_INTERFACE_NAME} INTERFACE)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE ThreadManager_Interface)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE ShaderCompiler)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE WindowSystem_Interface)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE TimerSystem)
target_include_directories(${PROJECT_INTERFACE_NAME} INTERFACE header)

target_precompile_headers(${PROJECT_NAME}
  PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/private/pch.h"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()