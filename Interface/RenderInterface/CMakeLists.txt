set(PROJECT_NAME Rendering)
set(PROJECT_INTERFACE_NAME ${PROJECT_NAME}_Interface)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

# `ScriptRenderBridge.cpp` 单独成一个静态库（csharp-render-backend-triangle 任务 2.2）：
# CoreTests 也要链接**同一份**实现，如果两个目标各编一次，同一个源文件在 compile_commands.json
# 里就有两套宏，`build.py` 的生成环境选择会以 "different macro sets" 拒绝（实测过）。
# `ThreadData.cpp`（`ThreadID::Get` / `ThreadLocalID::Get`）跟着独立出来：`ResourceHandleKeyData::Create`
# 是内联的，链接 `ScriptRenderBridge` 的目标也必须有这两个符号；两个目标各定义一份会重复定义。
list(FILTER Source_List EXCLUDE REGEX "private/(ScriptRenderBridge|ThreadData)\\.cpp$")

add_library(RenderThreadData STATIC private/ThreadData.cpp)
target_include_directories(RenderThreadData PUBLIC header ${CMAKE_BINARY_DIR}/codegen/Interface_RenderInterface)
target_link_libraries(RenderThreadData PRIVATE CACore)

add_library(ScriptRenderBridge STATIC private/ScriptRenderBridge.cpp)
target_include_directories(ScriptRenderBridge PUBLIC header ${CMAKE_BINARY_DIR}/codegen/Interface_RenderInterface)
target_link_libraries(ScriptRenderBridge PUBLIC RenderThreadData)
target_link_libraries(ScriptRenderBridge PRIVATE CACore ThreadManager_Interface CAGeneralReourceSystem_Interface
    IOManager WindowSystem_Interface ShaderCompiler TimerSystem)
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ScriptRenderBridge PROPERTY CXX_STANDARD 20)
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
# `ScriptRenderBridge.cpp` 的实现由独立静态库提供（见文件开头）；Rendering 的消费者（CSharpHost、
# GPUBackendTester）通过这一条链接拿到它。
target_link_libraries(${PROJECT_NAME} PUBLIC ScriptRenderBridge)
target_link_libraries(${PROJECT_NAME} PUBLIC RenderThreadData)
# `CRenderBackend.h` 的 include 链里有 `CAResource/ResourceManagingSystem.h`：原来的编译单元从别处
# 传递的目录拿到它，而 `ScriptRenderBridge.cpp`（csharp-render-backend-triangle 任务 2.2）在独立 TU 里
# 直接包含 `CRenderBackend.h`，必须显式带上这个接口目标的 include 目录。
target_link_libraries(${PROJECT_NAME} PRIVATE CAGeneralReourceSystem_Interface)

target_precompile_headers(${PROJECT_NAME}
  PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/private/pch.h"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()