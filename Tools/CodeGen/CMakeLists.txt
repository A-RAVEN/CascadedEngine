# Build-time CodeGen tool: extracts a linear type table from annotated C++ types using libclang.
#
# libclang is a *build-time* parser dependency for this tool ONLY. The engine itself does NOT link
# libclang (it stays pure MSVC cl.exe). The 100MB libclang.dll is not committed to the engine repo
# (consistent with the project's FetchContent/CPM pattern, which downloads deps rather than
# committing binaries) — we reference a directory containing it, defaulting to CACodeGen's vendored
# copy. Override with -DCALIB_CLANG_DIR=<dir-that-contains-bin/x64/libclang.dll>.
set(CALIB_CLANG_DIR "E:/Projects/CACodeGen/ExternalLibraries/LLVM" CACHE PATH
    "Dir containing bin/x64/libclang.dll + lib/x64/libclang.lib + include/clang-c")

if(NOT EXISTS "${CALIB_CLANG_DIR}/bin/x64/libclang.dll")
    message(WARNING "CALIB_CLANG_DIR '${CALIB_CLANG_DIR}' has no libclang.dll; CodeGen tool will NOT be built. Set -DCALIB_CLANG_DIR to a vendored LLVM dir.")
    return()
endif()

add_library(CLANG_LIB SHARED IMPORTED GLOBAL)
set_property(TARGET CLANG_LIB PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${CALIB_CLANG_DIR}/include")
set_property(TARGET CLANG_LIB PROPERTY IMPORTED_LOCATION "${CALIB_CLANG_DIR}/bin/x64/libclang.dll")
set_property(TARGET CLANG_LIB PROPERTY IMPORTED_IMPLIB  "${CALIB_CLANG_DIR}/lib/x64/libclang.lib")

add_executable(CodeGen CodeGenMain.cpp)
target_link_libraries(CodeGen PRIVATE CLANG_LIB)
set_property(TARGET CodeGen PROPERTY CXX_STANDARD 20)

# Copy the runtime libclang.dll alongside the tool so a fresh build runs without PATH gymnastics.
add_custom_command(TARGET CodeGen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CALIB_CLANG_DIR}/bin/x64/libclang.dll"
            "$<TARGET_FILE_DIR:CodeGen>/libclang.dll")
