#pragma once
#include "CodeGenMeta.h"
// Companion of string_refl.h for the register channel: the CA_CODEGEN_REGISTER spelling appears only inside
// a string literal, so it must not register anything. (The register channel still has to READ the quoted
// name of a real occurrence - see register_definition.h.)
namespace audit_ns
{
    static const char* kReg = "CA_CODEGEN_REGISTER(\"audit_ns::Ghost\")";
    struct Ghost { int a; float b; };
}
