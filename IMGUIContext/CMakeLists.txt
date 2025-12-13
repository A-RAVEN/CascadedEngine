set(PROJECT_NAME IMGUIContext)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} STATIC ${Header_List} ${Source_List})

target_include_directories(${PROJECT_NAME} PUBLIC header)
target_include_directories(${PROJECT_NAME} PRIVATE header/IMGUIContext)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering)
target_link_libraries(${PROJECT_NAME} PRIVATE CAGeneralReourceSystem_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE WindowSystem_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE IMGUI)
target_link_libraries(${PROJECT_NAME} PRIVATE glm)
target_link_libraries(${PROJECT_NAME} PRIVATE TimerSystem_Impl)
target_link_libraries(${PROJECT_NAME} PRIVATE stb)
CA_SETUP_MODULE(${PROJECT_NAME})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()