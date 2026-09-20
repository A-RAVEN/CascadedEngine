#pragma once
#include "CodeGenMeta.h"
// Control for the register channel: a real registration of a fully defined type. The register macro has no
// trailing semicolon in its own body, so the call site must supply one.
CA_CODEGEN_REGISTER("audit_ns::RegOK");
namespace audit_ns
{
    struct RegOK { int a; float b; };
}
