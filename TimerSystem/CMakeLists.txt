set(PROJECT_NAME TimerSystem)
project(${PROJECT_NAME})

file(GLOB_RECURSE Header_List "*.h")
file(GLOB_RECURSE Source_List "*.cpp")
add_library(${PROJECT_NAME} STATIC ${Header_List} ${Source_List})
target_link_libraries(${PROJECT_NAME} PUBLIC CACore)
target_include_directories(${PROJECT_NAME} PUBLIC header)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}")


if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
  set_property(TARGET ${PROJECT_INTERFACE_NAME} PROPERTY CXX_STANDARD 20)
endif()