set(PROJECT_NAME Rendering)
set(PROJECT_INTERFACE_NAME ${PROJECT_NAME}_Interface)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} STATIC ${Header_List} ${Source_List})

target_include_directories(${PROJECT_NAME} PUBLIC header)
target_link_libraries(${PROJECT_NAME} PUBLIC WindowSystem_Interface)
target_link_libraries(${PROJECT_NAME} PUBLIC ShaderCompiler)
#TODO: Move To System Manager
target_link_libraries(${PROJECT_NAME} PUBLIC TimerSystem)
#TODO: Move To System Manager
target_link_libraries(${PROJECT_NAME} PUBLIC IOManager)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)

target_precompile_headers(${PROJECT_NAME}
  PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/private/pch.h"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()