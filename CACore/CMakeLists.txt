set(PROJECT_NAME CACore)
#project (${PROJECT_NAME})
file(GLOB_RECURSE Header_List "*.h")
file(GLOB_RECURSE Source_List "*.cpp")

CPMAddPackage(
  NAME hash-library
  GIT_TAG hash_library_v8
  GITHUB_REPOSITORY stbrumme/hash-library
)

add_library(${PROJECT_NAME} STATIC ${Source_List} ${Header_List})
target_link_libraries(${PROJECT_NAME} PUBLIC CASTL)
target_link_libraries(${PROJECT_NAME} PRIVATE magic_enum)
target_link_libraries(${PROJECT_NAME} PRIVATE hash-library)
target_include_directories(${PROJECT_NAME} PUBLIC header)
target_include_directories(${PROJECT_NAME} INTERFACE header)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PUBLIC "/Zc:preprocessor")
endif()
