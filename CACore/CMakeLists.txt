set(PROJECT_NAME CACore)
#project (${PROJECT_NAME})
file(GLOB_RECURSE Header_List "*.h")
file(GLOB_RECURSE Source_List "*.cpp")

#Format library
CPMAddPackage(
  NAME fmt
  GIT_TAG 11.1.3
  GITHUB_REPOSITORY fmtlib/fmt
)

#Common Hash Library
CPMAddPackage(
  NAME hash-library
  GIT_TAG hash_library_v8
  GITHUB_REPOSITORY stbrumme/hash-library
)

#KomiHash Library
CPMAddPackage(
  NAME komihash
  GIT_TAG 5.10
  GITHUB_REPOSITORY avaneev/komihash
)

CPMAddPackage(
    NAME mimalloc
    GIT_TAG v3.0.8
    GITHUB_REPOSITORY microsoft/mimalloc
    OPTIONS
    "MI_USE_CXX ON"
    "MI_OVERRIDE ON"
)

CPMAddPackage(
  NAME magic_enum
  VERSION 0.9.7
  GITHUB_REPOSITORY Neargye/magic_enum
)

file(GLOB HashLib_SourceList "${hash-library_SOURCE_DIR}/*.cpp")
add_library(HashLib STATIC ${HashLib_SourceList})
target_include_directories(HashLib PUBLIC ${hash-library_SOURCE_DIR})
target_include_directories(HashLib PUBLIC ${komihash_SOURCE_DIR})


add_library(${PROJECT_NAME} STATIC ${Source_List} ${Header_List})
target_link_libraries(${PROJECT_NAME} PUBLIC mimalloc)
target_link_libraries(${PROJECT_NAME} PUBLIC HashLib)
target_link_libraries(${PROJECT_NAME} PUBLIC fmt)
target_link_libraries(${PROJECT_NAME} PUBLIC magic_enum)
target_include_directories(${PROJECT_NAME} PUBLIC header)
target_include_directories(${PROJECT_NAME} INTERFACE header)
target_compile_definitions(${PROJECT_NAME} INTERFACE USING_EASTL=0)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PUBLIC "/Zc:preprocessor")
endif()
