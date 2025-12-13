# CMakeList.txt: 顶层 CMake 项目文件，在此处执行全局配置
# 并包含子项目。
#
cmake_minimum_required (VERSION 3.12)
include("CMake/CACMakeUtils.cmake")

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

if (POLICY CMP0140)
  cmake_policy(SET CMP0140 NEW)
endif()

project ("CascadedEngine")
add_compile_definitions(NOMINMAX=1)
set(ENV{HTTP_PROXY} "http://127.0.0.1:7890")
set(ENV{HTTPS_PROXY} "http://127.0.0.1:7890")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /MANIFEST:NO")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /MANIFEST:NO")
set(CXX_STANDARD 20)
if (CMAKE_VERSION VERSION_GREATER 3.12)
	set(CMAKE_CXX_STANDARD 20)
endif()

SET(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR}/installed)

# External Project Help
add_subdirectory ("ExternalLib")

# 包含子项目。
#add_subdirectory ("CASTL")
add_subdirectory ("CACore")
add_subdirectory ("IMGUIContext")
add_subdirectory ("TimerSystem")
add_subdirectory ("TimerSystem_Impl")
add_subdirectory ("ThreadManager")
add_subdirectory ("WindowSystem")
add_subdirectory ("GeneralResources")
add_subdirectory ("ShaderCompiler")
add_subdirectory ("IOManager")
add_subdirectory ("IOManager_FS")
add_subdirectory ("RenderInterface")
add_subdirectory ("ShaderCompilerSlang")
#add_subdirectory ("VulkanRenderBackend")
add_subdirectory ("D3D12RenderBackend")
#add_subdirectory ("DotNetHost")
#add_subdirectory ("VulkanRendererBackendTester")
add_subdirectory ("D3D12RenderBackendTester")
add_subdirectory ("CoreTests")
