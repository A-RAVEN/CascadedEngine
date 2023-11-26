set(PROJECT_NAME ShaderCompiler)
project(${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_library(${PROJECT_NAME} MODULE ${Header_List} ${Source_List})


if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
set(ADDITIONAL_LIBRARY_DEPENDENCIES
    "shaderc_combinedd"
    "spirv-cross-cored"
    "spirv-cross-glsld"
    "spirv-cross-hlsld"
    "spirv-cross-msld"
    "spirv-cross-reflectd"
    "spirv-cross-utild"
    "spirv-cross-cppd"
    "spirv-cross-cd"
    "spirv-cross-utild"
)
elseif("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
set(ADDITIONAL_LIBRARY_DEPENDENCIES
    "shaderc_combined"
    "spirv-cross-core"
    "spirv-cross-glsl"
    "spirv-cross-hlsl"
    "spirv-cross-msl"
    "spirv-cross-reflect"
    "spirv-cross-util"
    "spirv-cross-cpp"
    "spirv-cross-c"
    "spirv-cross-util"
)
endif()


target_link_directories(${PROJECT_NAME} PUBLIC
    "$ENV{VK_SDK_PATH}/Lib"
)

target_link_libraries(${PROJECT_NAME} PUBLIC "${ADDITIONAL_LIBRARY_DEPENDENCIES}")

target_include_directories(${PROJECT_NAME} PUBLIC "$ENV{VK_SDK_PATH}/Include")
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}")
#include(${CMAKE_SOURCE_DIR}/CMake/ExternalProjects.cmake)
#link_llvm(ShaderCompiler)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()
