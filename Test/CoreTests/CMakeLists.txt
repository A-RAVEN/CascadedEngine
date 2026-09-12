set(PROJECT_NAME CoreTests)
project (${PROJECT_NAME})

file(GLOB_RECURSE Source_List "*.cpp")
file(GLOB_RECURSE Header_List "*.h")

add_executable(${PROJECT_NAME} ${Header_List} ${Source_List})

target_link_libraries(${PROJECT_NAME} PRIVATE CACore)

target_link_libraries(${PROJECT_NAME} PRIVATE glm)

# Task 5.1 ABI-guard compile proof: CoreTests includes the CodeGen tool's demo type
# (Tools/CodeGen/TestEmit.h) + the emitted reflection (Tools/CodeGen/generated) so the generated
# TypeDescriptor<...> is compiled and its fieldCount is asserted against CA_REFLECTION.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Tools/CodeGen)



if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()