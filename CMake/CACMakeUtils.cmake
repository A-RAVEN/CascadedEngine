function(CA_SETUP_MODULE target)
if (NOT TARGET ${target})
message(FATAL_ERROR "CA_SETUP_MODULE: Target '${target}' does not exist.")
endif()

string(REGEX REPLACE "[- ]" "_" _norm "${target}")

set(_macro_static "STATIC_BUILT_${_norm}")

get_target_property(_type ${target} TYPE)
if (_type STREQUAL "STATIC_LIBRARY")
set(_is_static_val "1")
else()
set(_is_static_val "0")
endif()

message(STATUS "CA_SETUP_MODULE: target='${target}' "
                "_is_static_val='${_is_static_val}'"
                "_type='${_type}'"
)

target_compile_definitions(${target}
PUBLIC
${_macro_static}=${_is_static_val}
)
target_compile_definitions(${target}
PRIVATE
CA_STATIC_BUILD=${_is_static_val}
CA_MODULE_NAME=${target}
)
endfunction()