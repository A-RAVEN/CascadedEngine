set(PROJECT_NAME CoreTests)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_executable(${PROJECT_NAME} ${Header_List} ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE CACore)

target_link_libraries(${PROJECT_NAME} PRIVATE glm)

# ABI-guard compile proof: CoreTests includes the CodeGen tool's demo type
# (Tools/CodeGen/TestEmit.h) + the emitted reflection next to the fixtures' OWNER module. 归属目录决定产物
# 落在哪：夹具的头在 Tools/CodeGen 下，但消费它的编译单元在这个模块里，所以产物跟着这个模块走 ——
# 出仓之后它在构建目录的 `codegen/Test_CoreTests/`，源码树里没有 `generated/` 了。
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Tools/CodeGen ${CMAKE_BINARY_DIR}/codegen/Test_CoreTests ${CMAKE_SOURCE_DIR}/Interface/RenderInterface/private ${CMAKE_BINARY_DIR}/codegen/Interface_RenderInterface ${CMAKE_SOURCE_DIR}/Interface/RenderInterface/header)



if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()
