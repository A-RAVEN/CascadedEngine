set(PROJECT_NAME GPUBackendTester)
project (${PROJECT_NAME})

set(Source_List
	Main.cpp
	STB_Impl.cpp
	private/MiniDump.cpp
)
set(Header_List
	private/MiniDump.h
)

add_executable(${PROJECT_NAME} ${Header_List} ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE CACore)
target_link_libraries(${PROJECT_NAME} PRIVATE TimerSystem)
target_link_libraries(${PROJECT_NAME} PRIVATE ThreadManager_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE Rendering)
target_link_libraries(${PROJECT_NAME} PRIVATE ShaderCompiler)
target_link_libraries(${PROJECT_NAME} PRIVATE CAGeneralReourceSystem_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE WindowSystem_Interface)
target_link_libraries(${PROJECT_NAME} PRIVATE glm)
target_link_libraries(${PROJECT_NAME} PRIVATE IMGUI)
target_link_libraries(${PROJECT_NAME} PRIVATE assimp)
target_link_libraries(${PROJECT_NAME} PRIVATE stb)
target_link_libraries(${PROJECT_NAME} PRIVATE TimerSystem_Impl)
target_link_libraries(${PROJECT_NAME} PRIVATE Jolt)
target_link_libraries(${PROJECT_NAME} PRIVATE IOManager)
target_link_libraries(${PROJECT_NAME} PRIVATE magic_enum)
target_link_libraries(${PROJECT_NAME} PRIVATE IMGUIContext)
target_link_libraries(${PROJECT_NAME} PRIVATE ScriptHost)
target_link_libraries(${PROJECT_NAME} PRIVATE dbghelp)

# 生成的绑定产物（`csharp-window-creation` 任务 2.8）：测试要拿弱句柄，只能走 C ABI 的
# `cawindow__IWindowSystem__NewWindow` / `__ResolveHandle`，所以要 include `<限定名线形>.Bindings.cpp`。
# 目录与 CSharpHost/CMakeLists.txt 里那条一致（同一个生成步骤写出的）。
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}/codegen/bindings
    ${CMAKE_SOURCE_DIR}/WindowSystem/header/CAWindow
    # csharp-reverse-interface-binding 8.1：反向资源产物按基名 include `IResource.h`（同上）。
    ${CMAKE_SOURCE_DIR}/GeneralResources/header/CAResource
    # csharp-resource-asset-foundation 4.2：门面产物按基名 include `IResourceRegistry.h`（同上）。
    ${CMAKE_SOURCE_DIR}/Interface/ScriptHost/header/ScriptHost)

#target_link_libraries(${PROJECT_NAME} PRIVATE IOManager_FS)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()