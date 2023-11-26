set(PROJECT_NAME RenderInterface)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}")

target_precompile_headers(${PROJECT_NAME}
  PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/private/pch.h"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()