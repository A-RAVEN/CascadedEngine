set(PROJECT_NAME CAGeneralReourceSystem)
set(PROJECT_INTERFACE_NAME ${PROJECT_NAME}_Interface)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_include_directories(${PROJECT_NAME} PRIVATE header)

add_library(${PROJECT_INTERFACE_NAME} INTERFACE)
target_include_directories(${PROJECT_INTERFACE_NAME} INTERFACE header)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()