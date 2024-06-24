set(PROJECT_NAME ImguiDisplay)
set(PROJECT_INTERFACE_NAME ${PROJECT_NAME}_Interface)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_executable(${PROJECT_NAME} ${Header_List} ${Source_List})

target_include_directories(${PROJECT_NAME} PRIVATE header)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE CAGeneralReourceSystem_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE WindowSystem_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE IMGUI)

add_library(${PROJECT_INTERFACE_NAME} INTERFACE)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE CACore)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE ThreadManager_Interface)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE Rendering_Interface)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE CAGeneralReourceSystem_Interface)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE WindowSystem_Interface)
target_include_directories(${PROJECT_INTERFACE_NAME} INTERFACE header)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()