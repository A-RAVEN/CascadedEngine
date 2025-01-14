set(PROJECT_NAME CACore)
#project (${PROJECT_NAME})
file(GLOB_RECURSE Header_List "*.h")
file(GLOB_RECURSE Source_List "*.cpp")

CPMAddPackage(
  NAME hash-library
  GIT_TAG hash_library_v8
  GITHUB_REPOSITORY stbrumme/hash-library
)

CPMAddPackage(
  NAME komihash
  GIT_TAG 5.10
  GITHUB_REPOSITORY avaneev/komihash
)
file(GLOB HashLib_SourceList "${hash-library_SOURCE_DIR}/*.cpp")
add_library(HashLib STATIC ${HashLib_SourceList})
target_include_directories(HashLib PUBLIC ${hash-library_SOURCE_DIR})
target_include_directories(HashLib PUBLIC ${komihash_SOURCE_DIR})


add_library(${PROJECT_NAME} STATIC ${Source_List} ${Header_List})
target_link_libraries(${PROJECT_NAME} PUBLIC CASTL)
target_link_libraries(${PROJECT_NAME} PUBLIC HashLib)
target_link_libraries(${PROJECT_NAME} PRIVATE magic_enum)
target_include_directories(${PROJECT_NAME} PUBLIC header)
target_include_directories(${PROJECT_NAME} INTERFACE header)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PUBLIC "/Zc:preprocessor")
endif()
