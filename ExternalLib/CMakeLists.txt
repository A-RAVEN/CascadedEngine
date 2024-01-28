
set(PROJECT_NAME ExternalProjects)
project (${PROJECT_NAME})

#VMA
set(VMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator)
add_subdirectory(${VMA_DIR})

#GLFW
set(GLFW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glfw)
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "Build the GLFW example programs")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "Build the GLFW test programs")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "Build the GLFW documentation")
set(GLFW_INSTALL OFF CACHE INTERNAL "Generate installation target")
add_subdirectory(${GLFW_DIR})

#DXC
set(LLVM_PARALLEL_COMPILE_JOBS 1)
set(LLVM_PARALLEL_LINK_JOBS 1)
set(LLVM_TARGETS_TO_BUILD "None")
set(DXC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/DirectXShaderCompiler)
include(${DXC_DIR}/cmake/caches/PredefinedParams.cmake)
set(LLVM_INCLUDE_TESTS OFF)
set(HLSL_INCLUDE_TESTS OFF)
set(ENABLE_SPIRV_CODEGEN ON)
add_subdirectory(${DXC_DIR})

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