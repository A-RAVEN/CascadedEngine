
include(FetchContent)
set(PROJECT_NAME ExternalProjects)
project (${PROJECT_NAME})

#CPM
file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.2/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

CPMAddPackage(
  NAME VulkanMemoryAllocator
  GIT_TAG v3.1.0
  GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
)

#DMA Only Master Works For Now
CPMAddPackage(
  NAME D3D12MemoryAllocator
  GIT_TAG master
  GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
)

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

CPMAddPackage(
  NAME glm
  GIT_TAG 1.0.1
  GITHUB_REPOSITORY g-truc/glm
)

CPMAddPackage(
  NAME imgui
  VERSION 1.91.6-docking
  GITHUB_REPOSITORY ocornut/imgui
  DOWNLOAD_ONLY TRUE
)
file(GLOB IMGUI_SourceList "${imgui_SOURCE_DIR}/*.cpp")
add_library(IMGUI STATIC ${IMGUI_SourceList})
target_include_directories(IMGUI PUBLIC ${imgui_SOURCE_DIR})



CPMAddPackage(
  NAME assimp
  GIT_TAG v5.4.3
  GITHUB_REPOSITORY assimp/assimp
  OPTIONS
	"ASSIMP_WARNINGS_AS_ERRORS OFF"
	"ASSIMP_INJECT_DEBUG_POSTFIX OFF"
	"ASSIMP_INSTALL OFF"
	"ASSIMP_BUILD_TESTS OFF"
)

CPMAddPackage(
  NAME DirectX-Headers
  VERSION 1.614.1
  GITHUB_REPOSITORY microsoft/DirectX-Headers
  EXCLUDE_FROM_ALL TRUE
)

#slang
#[[
set(SLANG_SLANG_LLVM_FLAVOR DISABLE)
set(SLANG_LIB_TYPE STATIC)
set(SLANG_ENABLE_SLANG_GLSLANG ON)
set(SLANG_ENABLE_TESTS OFF)
set(SLANG_ENABLE_GFX OFF)
set(SLANG_ENABLE_SLANGD OFF)
set(SLANG_ENABLE_SLANGC OFF)
set(SLANG_ENABLE_SLANGRT OFF)
set(SLANG_ENABLE_EXAMPLES OFF)
add_compile_definitions(SLANG_ENABLE_GLSLANG_STATIC_LINK)
set(SLANG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/slang)
add_subdirectory(${SLANG_DIR})
]]

CPMAddPackage(
  NAME slang
  GIT_TAG master
  GITHUB_REPOSITORY A-RAVEN/slang
  GIT_SUBMODULES_RECURSE TRUE
  GIT_PROGRESS TRUE
  EXCLUDE_FROM_ALL TRUE
  OPTIONS
	"SLANG_SLANG_LLVM_FLAVOR DISABLE"
	"SLANG_LIB_TYPE STATIC"
	"SLANG_ENABLE_SLANG_GLSLANG ON"
	"SLANG_ENABLE_DXIL ON"
	"SLANG_ENABLE_TESTS OFF"
	"SLANG_ENABLE_GFX OFF"
	"SLANG_ENABLE_SLANGD OFF"
	"SLANG_ENABLE_SLANGRT OFF"
	"SLANG_ENABLE_EXAMPLES OFF"
)

#STB
if(NOT TARGET stb)
CPMAddPackage(
  NAME stb
  GIT_TAG master
  GITHUB_REPOSITORY nothings/stb
  DOWNLOAD_ONLY TRUE
)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
endif()

CPMAddPackage(
  NAME JoltPhysics
  VERSION 5.2.0
  GITHUB_REPOSITORY jrouwe/JoltPhysics
  SOURCE_SUBDIR Build
  EXCLUDE_FROM_ALL TRUE
  OPTIONS
	"CROSS_PLATFORM_DETERMINISTIC ON"
	"OVERRIDE_CXX_FLAGS OFF"
)

add_library(VulkanSDK SHARED IMPORTED GLOBAL)
set_property(TARGET VulkanSDK PROPERTY INTERFACE_INCLUDE_DIRECTORIES $ENV{VK_SDK_PATH}/Include)
set_property(TARGET VulkanSDK PROPERTY IMPORTED_IMPLIB $ENV{VK_SDK_PATH}/Lib/vulkan-1.lib)

FetchContent_Declare(
  enum_name
  GIT_REPOSITORY https://github.com/mguludag/enum_name.git
  GIT_TAG main # or the specific tag or branch you want to use
)
FetchContent_MakeAvailable(enum_name)