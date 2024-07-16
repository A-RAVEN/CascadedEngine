set(PROJECT_NAME DotNetHost)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_executable(${PROJECT_NAME} ${Header_List} ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE CACore)

target_include_directories(${PROJECT_NAME} PRIVATE "C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/8.0.4/runtimes/win-x64/native")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()