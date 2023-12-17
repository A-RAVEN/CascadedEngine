set(PROJECT_NAME CAGeneralReourceSystem)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

include(${CMAKE_SOURCE_DIR}/CMake/ExternalProjects.cmake)
#link_imgui(Source_List)

#add_executable(${PROJECT_NAME} ${Header_List} ${Source_List} "private/KeyCodes.h")
add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()