
set(PROJECT_NAME ExternalProjects)
project (${PROJECT_NAME})

include(${CMAKE_CURRENT_SOURCE_DIR}/CPM/cmake/CPM.cmake)
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

#LLVM(Clang Only)
#[[
set(LLVM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/llvm-project/llvm)
set(LLVM_ENABLE_PROJECTS lld;clang)
set(CMAKE_BUILD_TYPE MinSizeRel)
set(LLVM_BUILD_TOOLS OFF)
set(LLVM_ENABLE_LLD ON)
set(LLVM_TARGETS_TO_BUILD X86)
set(LLVM_PARALLEL_COMPILE_JOBS 2)
set(LLVM_PARALLEL_LINK_JOBS 2)
message("LLVM Build Type ${CMAKE_BUILD_TYPE}")
add_subdirectory(${LLVM_DIR})
]]
set(LLVM_PARALLEL_COMPILE_JOBS 1)
set(LLVM_PARALLEL_LINK_JOBS 1)
set(LLVM_TARGETS_TO_BUILD "None")
#set(DXC_COVERAGE ON)
set(DXC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/DirectXShaderCompiler)
include(${DXC_DIR}/cmake/caches/PredefinedParams.cmake)
set(LLVM_INCLUDE_TESTS OFF)
set(HLSL_INCLUDE_TESTS OFF)
set(ENABLE_SPIRV_CODEGEN ON)
add_subdirectory(${DXC_DIR})
#[[
Set(FETCHCONTENT_QUIET FALSE)
Set(ENABLED_LLVM_PROJECTS "lld\\;clang")
CPMAddPackage(
    NAME llvm-project
    GIT_REPOSITORY https://github.com/llvm/llvm-project.git
    GIT_TAG main
    GIT_PROGRESS TRUE
    SOURCE_SUBDIR llvm
    OPTIONS
        "LLVM_ENABLE_PROJECTS ${ENABLED_LLVM_PROJECTS}"
        "LLVM_ENABLE_LLD ON"
        "LLVM_PARALLEL_COMPILE_JOBS 2"
        "LLVM_PARALLEL_LINK_JOBS 2"
    -DLLVM_ENABLE_PROJECTS="lld;clang"
)]]