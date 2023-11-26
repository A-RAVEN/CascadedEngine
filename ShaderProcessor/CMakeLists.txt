set(PROJECT_NAME ShaderProcessor)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_executable(${PROJECT_NAME} ${Header_List} ${Source_List})

include(${CMAKE_SOURCE_DIR}/CMake/ExternalProjects.cmake)
#link_llvm(${PROJECT_NAME})
#find_package(dxc REQUIRED CONFIG)
link_dxc(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()

