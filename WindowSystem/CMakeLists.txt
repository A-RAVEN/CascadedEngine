set(PROJECT_NAME WindowSystem)
set(PROJECT_INTERFACE_NAME ${PROJECT_NAME}_Interface)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

CPMAddPackage(
  NAME glfw
  GIT_TAG 3.4
  GITHUB_REPOSITORY glfw/glfw
  OPTIONS
	"GLFW_BUILD_EXAMPLES OFF"
	"GLFW_BUILD_TESTS OFF"
	"GLFW_BUILD_DOCS OFF"
	"GLFW_INSTALL OFF"
)

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})
target_include_directories(${PROJECT_NAME} PRIVATE "private")
target_include_directories(${PROJECT_NAME} PRIVATE header)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw)

add_library(${PROJECT_INTERFACE_NAME} INTERFACE)
target_include_directories(${PROJECT_INTERFACE_NAME} INTERFACE header)
target_link_libraries(${PROJECT_INTERFACE_NAME} INTERFACE CACore)