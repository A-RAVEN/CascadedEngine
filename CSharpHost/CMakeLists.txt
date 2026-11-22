set(PROJECT_NAME CSharpHost)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

# nethost：用已装的 .NET Host pack 里的那一份（不写死版本号 —— 原型把它写死成 8.0.4，本机没有那个版本）。
# 找不到时**不让构建失败**：引擎照样能编出来，运行期由 CSharpHost 报"找不到 nethost / hostfxr 并列出找过的路径"
# （这条是 csharp-host spec 里的硬判据，交给运行期报比交给 configure 报更符合"失败必须响"）。
file(GLOB NETHOST_CANDIDATES
    "$ENV{ProgramFiles}/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/*/runtimes/win-x64/native")
list(SORT NETHOST_CANDIDATES COMPARE NATURAL)
list(REVERSE NETHOST_CANDIDATES)
list(GET NETHOST_CANDIDATES 0 NETHOST_DIR)
if (NETHOST_DIR)
    message(STATUS "CSharpHost: nethost 取自 ${NETHOST_DIR}")
    add_library(nethost SHARED IMPORTED GLOBAL)
    target_include_directories(nethost INTERFACE ${NETHOST_DIR})
    set_property(TARGET nethost PROPERTY IMPORTED_IMPLIB_RELEASE "${NETHOST_DIR}/nethost.lib")
    set_property(TARGET nethost PROPERTY IMPORTED_LOCATION_RELEASE "${NETHOST_DIR}/nethost.dll")
    set_property(TARGET nethost PROPERTY IMPORTED_IMPLIB_DEBUG "${NETHOST_DIR}/nethost.lib")
    set_property(TARGET nethost PROPERTY IMPORTED_LOCATION_DEBUG "${NETHOST_DIR}/nethost.dll")
else()
    message(STATUS "CSharpHost: 没找到 nethost（引擎仍可构建，运行期会报错并列出找过的路径）")
endif()

# MODULE：产出 CSharpHost.dll，由引擎在运行时按名字加载（不静态链进宿主）
add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})

target_include_directories(${PROJECT_NAME} PUBLIC header)
# 生成的绑定产物（`IWindow.Bindings.cpp`、`CRenderBackend.Bindings.cpp`、`GPUGraph.Bindings.cpp`、
# `ShaderStruct.Bindings.cpp`、`GPUTexture.Bindings.cpp`、`WindowHandle.Bindings.cpp`，由 build.py 的
# 模型生成步骤写出）与它们 include 的接口头：这个模块直接编译这些产物，所以自己就带着函数表与 thunk。
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}/codegen/bindings
    ${CMAKE_SOURCE_DIR}/WindowSystem/header/CAWindow
    # csharp-reverse-interface-binding 7.1：反向资源产物 `#include "IResource.h"` 按基名找头
    # （与所有正向产物同一规则），所以 `CAResource` 目录要在这个目标上可见。
    ${CMAKE_SOURCE_DIR}/GeneralResources/header/CAResource)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ScriptHost)
# 5.1：桥退役后宿主直持原生后端（Rendering 的 `CRenderBackend` 实例），登记为编号 3。
# `CRenderBackend.h` 的 include 链还需要下面两个接口目录。
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE CAGeneralReourceSystem_Interface)

if (NETHOST_DIR)
    target_link_libraries(${PROJECT_NAME} PRIVATE nethost)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CASCADED_HAS_NETHOST=1)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:nethost> $<TARGET_FILE_DIR:${PROJECT_NAME}>)
endif()

CA_SETUP_MODULE(${PROJECT_NAME})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()
