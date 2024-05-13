
set(PROJECT_NAME ExternalProjects)
project (${PROJECT_NAME})

#VMA
set(VMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator)
add_subdirectory(${VMA_DIR})

#GLM
add_subdirectory(glm)

#GLFW
set(GLFW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glfw)
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "Build the GLFW example programs")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "Build the GLFW test programs")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "Build the GLFW documentation")
set(GLFW_INSTALL OFF CACHE INTERNAL "Generate installation target")
add_subdirectory(${GLFW_DIR})

#IMGUI
file(GLOB IMGUI_SourceList "${CMAKE_CURRENT_SOURCE_DIR}/imgui/*.cpp")
add_library(IMGUI STATIC ${IMGUI_SourceList})
target_include_directories(IMGUI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

#ZPP_Bits
#add_library(zpp_bits INTERFACE)
#target_include_directories(zpp_bits INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/zpp_bits)

#STB
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

#DXC
#[[
set(LLVM_PARALLEL_COMPILE_JOBS 1)
set(LLVM_PARALLEL_LINK_JOBS 1)
set(LLVM_TARGETS_TO_BUILD "None")
set(DXC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/DirectXShaderCompiler)
include(${DXC_DIR}/cmake/caches/PredefinedParams.cmake)
set(LLVM_INCLUDE_TESTS OFF)
set(HLSL_INCLUDE_TESTS OFF)
set(ENABLE_SPIRV_CODEGEN ON)
add_subdirectory(${DXC_DIR})
]]

#ASSIMP
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE INTERNAL "Turn Off Warnings As Errors")
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE INTERNAL "Turn Off Postfix")
set(ASSIMP_INSTALL OFF CACHE INTERNAL "Turn Off Postfix")
set(ASSIMP_BUILD_TESTS OFF CACHE INTERNAL "Turn Off Tests")
set(ASSIMP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assimp)
add_subdirectory(${ASSIMP_DIR})


#EASTL
set(EASTL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/EASTL)
add_subdirectory(${EASTL_DIR})


#slang
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

#jolt physics
set(CROSS_PLATFORM_DETERMINISTIC ON)
set(OVERRIDE_CXX_FLAGS OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/JoltPhysics/Build)