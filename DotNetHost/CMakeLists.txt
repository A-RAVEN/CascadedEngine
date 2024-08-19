set(PROJECT_NAME DotNetHost)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

set(DOTNETSDK_PATH "C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/8.0.4/runtimes/win-x64/native")
add_library(nethost SHARED IMPORTED GLOBAL)
target_include_directories(nethost INTERFACE ${DOTNETSDK_PATH})
set_property(TARGET nethost PROPERTY IMPORTED_IMPLIB_RELEASE "${DOTNETSDK_PATH}/nethost.lib")
set_property(TARGET nethost PROPERTY IMPORTED_LOCATION_RELEASE "${DOTNETSDK_PATH}/nethost.dll")
set_property(TARGET nethost PROPERTY IMPORTED_IMPLIB_DEBUG "${DOTNETSDK_PATH}/nethost.lib")
set_property(TARGET nethost PROPERTY IMPORTED_LOCATION_DEBUG "${DOTNETSDK_PATH}/nethost.dll")

add_executable(${PROJECT_NAME} ${Header_List} ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE nethost)

add_custom_command (TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:nethost> $<TARGET_FILE_DIR:${PROJECT_NAME}>
)


#[[target_include_directories(${PROJECT_NAME} PRIVATE ${DOTNETSDK_PATH})
target_link_directories(${PROJECT_NAME} PRIVATE
    ${DOTNETSDK_PATH}
)]]#
#target_link_libraries(${PROJECT_NAME} PRIVATE "nethost.lib")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()