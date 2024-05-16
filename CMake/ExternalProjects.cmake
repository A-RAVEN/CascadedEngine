#[[

include(FindGit)
find_package(Git)

if(NOT Git_FOUND)
    message(FATAL_ERROR "Git Not Found!")
else()
    message(STATUS "Git Found!")
endif()

include (ExternalProject)
include(FetchContent)

set(FETCHCONTENT_QUIET FALSE)

set(LLVM_DIR ${CMAKE_BINARY_DIR}/ExternalLib/llvm-project/llvm/lib/cmake/llvm)
set(LIB_CLANG_SHARED_LIB_NAME "libclang${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(LIB_CLANG_STATIC_LIB_NAME "libclang${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(CLANG_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/ExternalLib/llvm-project/clang/include")


set(DXC_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/ExternalLib/DirectXShaderCompiler/include")
set(DXC_STATIC_LIB_DIRS "${CMAKE_BINARY_DIR}/ExternalLib/DirectXShaderCompiler/lib")
set(DXC_SHARED_LIB_DIRS "${CMAKE_BINARY_DIR}/ExternalLib/DirectXShaderCompiler/bin")
set(DXC_STATIC_LIB_NAMES "dxcompiler${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(DXC_SHARED_LIB_NAMES "dxcompiler${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(DXC_SHARED_LIB_PATH "${DXC_SHARED_LIB_DIRS}/${DXC_SHARED_LIB_NAMES}")
function(link_dxc)
    target_link_libraries(${ARGV0} PRIVATE dxcompiler)
    target_include_directories(${ARGV0} PUBLIC "${DXC_INCLUDE_DIRS}")
    target_link_directories(${ARGV0} PRIVATE ${DXC_STATIC_LIB_DIRS})
    target_link_libraries(${ARGV0} PRIVATE ${DXC_STATIC_LIB_NAMES})
    target_link_libraries(${ARGV0} PRIVATE ${DXC_DIR})
    # copy dylibs on post build
    add_custom_command(TARGET ${ARGV0} POST_BUILD
        # DXC shared library
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${DXC_SHARED_LIB_PATH}
            $<TARGET_FILE_DIR:${ARGV0}>
    )
endfunction()

function(link_llvm)
    message("Try Link To LLVM Project")
    find_package(LLVM REQUIRED CONFIG)
    #include_directories(${LLVM_INCLUDE_DIRS})
    #include_directories(${CLANG_INCLUDE_DIRS})
    target_include_directories(${ARGV0} PUBLIC "${LLVM_INCLUDE_DIRS}")
    target_include_directories(${ARGV0} PUBLIC "${CLANG_INCLUDE_DIRS}")
    target_link_directories(${ARGV0} PRIVATE ${LLVM_LIBRARY_DIR})
    target_link_libraries(${ARGV0} PRIVATE ${LIB_CLANG_STATIC_LIB_NAME})
    set(LIB_CLANG_SHARED_LIB_PATH "${LLVM_BINARY_DIR}/bin/${LIB_CLANG_SHARED_LIB_NAME}")
    # copy resources on post build
    add_custom_command(TARGET ${ARGV0} POST_BUILD

    # LibClang shared library
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${LIB_CLANG_SHARED_LIB_PATH}
        $<TARGET_FILE_DIR:${ARGV0}>
    )
    message("   Target Project Name: ${ARGV0}")
    message("   LLVM Include Dir: ${LLVM_INCLUDE_DIRS}")
    message("   CLANG Include Dir: ${CLANG_INCLUDE_DIRS}")
    message("   LLVM Library Dir: ${LLVM_LIBRARY_DIR}")
    message("   LibClang Dylib Path: ${LIB_CLANG_SHARED_LIB_PATH}")
endfunction()

function(link_imgui THE_LIST)
    message("Try Link To IMGUI")
    file(GLOB IMGUI_SourceList "${CMAKE_SOURCE_DIR}/ExternalLib/imgui/*.cpp")
    list(APPEND ${THE_LIST} ${IMGUI_SourceList})
    foreach(X IN LISTS ${THE_LIST})
        message("${X}")
    endforeach()
    return(PROPAGATE ${THE_LIST})
endfunction()
]]