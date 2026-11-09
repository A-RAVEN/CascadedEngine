set(PROJECT_NAME Rendering)
set(PROJECT_INTERFACE_NAME ${PROJECT_NAME}_Interface)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

# `ThreadData.cpp`（`ThreadID::Get` / `ThreadLocalID::Get`）单独成一个静态库：
# `ResourceHandleKeyData::Create` 是内联的，凡是链接 Rendering 的目标都需要这两个符号；
# 两个目标各定义一份会重复定义。桥退役（5.1）之后这条抽离仍然需要。
list(FILTER Source_List EXCLUDE REGEX "private/ThreadData\\.cpp$")

add_library(RenderThreadData STATIC private/ThreadData.cpp)
target_include_directories(RenderThreadData PUBLIC header ${CMAKE_BINARY_DIR}/codegen/Interface_RenderInterface)
target_link_libraries(RenderThreadData PRIVATE CACore)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET RenderThreadData PROPERTY CXX_STANDARD 20)
endif()

add_library(${PROJECT_NAME} STATIC ${Header_List} ${Source_List})

target_include_directories(${PROJECT_NAME} PUBLIC header)
# This module's CodeGen output. It lives in the BUILD tree (build.py's codegen step writes it before any
# compilation); the source tree no longer carries generated code. PUBLIC, because other modules that use
# these types need it: the D3D12 backend hashes `CPipelineStateObject` through `HashObj`, and without the
# generated hash specializations on its include path that hash would silently fall back to whole-object
# bytes (padding included) instead of the field-wise hash.
# 归属目录 `Interface/RenderInterface` 在构建目录里写成 `Interface_RenderInterface`（`/` 换成 `_`）。
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_BINARY_DIR}/codegen/Interface_RenderInterface)
target_link_libraries(${PROJECT_NAME} PUBLIC WindowSystem_Interface)
target_link_libraries(${PROJECT_NAME} PUBLIC ShaderCompiler)
#TODO: Move To System Manager
target_link_libraries(${PROJECT_NAME} PUBLIC TimerSystem)
#TODO: Move To System Manager
target_link_libraries(${PROJECT_NAME} PUBLIC IOManager)
target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
# 桥（5.1）退役后线程标识仍在独立静态库里（见文件开头）。
target_link_libraries(${PROJECT_NAME} PUBLIC RenderThreadData)
# `CRenderBackend.h` 的 include 链里有 `CAResource/ResourceManagingSystem.h`：直接包含它的编译单元
# 必须显式带上这个接口目标的 include 目录。
target_link_libraries(${PROJECT_NAME} PRIVATE CAGeneralReourceSystem_Interface)

target_precompile_headers(${PROJECT_NAME}
  PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/private/pch.h"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()