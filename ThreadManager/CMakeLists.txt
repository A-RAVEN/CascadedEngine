set(PROJECT_NAME ThreadManager)
set(PROJECT_INTERFACE_NAME ${PROJECT_NAME}_Interface)
project(${PROJECT_NAME})

file(GLOB_RECURSE Header_List "*.h")
file(GLOB_RECURSE Source_List "*.cpp")
add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_include_directories(${PROJECT_NAME} PRIVATE header)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}")
target_precompile_headers(${PROJECT_NAME}
  PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/private/pch.h"
)

add_library(${PROJECT_INTERFACE_NAME} INTERFACE)
target_include_directories(${PROJECT_INTERFACE_NAME} INTERFACE header)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE CACore)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
  set_property(TARGET ${PROJECT_INTERFACE_NAME} PROPERTY CXX_STANDARD 20)
endif()