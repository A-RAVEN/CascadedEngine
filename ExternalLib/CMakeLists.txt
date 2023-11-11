
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

message(${CMAKE_SOURCE_DIR}/CMake/ExternalProjects.cmake)
include(${CMAKE_SOURCE_DIR}/CMake/ExternalProjects.cmake)
#LLVM
#set(LLVM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/llvm-project/llvm)
#unset(LLVM_ENABLE_PROJECTS CACHE)
#unset(LLVM_ENABLE_RUNTIMES CACHE)
#set(LLVM_ENABLE_PROJECTS "lld;clang" CACHE STRING INTERNAL)
#set(LLVM_ENABLE_RUNTIMES "all" CACHE STRING INTERNAL)
#set(LLVM_ENABLE_LLD ON CACHE INTERNAL "Link LLVM Using LLD")
#message(STATUS "${LLVM_ENABLE_RUNTIMES}")
#foreach(proj $CACHE{LLVM_ENABLE_PROJECTS})
#     MESSAGE(STATUS "${proj}")
#endforeach()
#add_subdirectory(${LLVM_DIR})
#[[
FetchContent_Declare(
	llvm
	GIT_REPOSITORY https://github.com/llvm/llvm-project.git
	GIT_TAG main
)

FetchContent_Populate(
    llvm                           # Recommendation: Stick close to the original name.
    GIT_REPOSITORY https://github.com/llvm/llvm-project.git
	GIT_TAG main
    SOURCE_DIR llvm              # (Relative) path within in the build directory.
)]]

#FetchContent_MakeAvailable(llvm)