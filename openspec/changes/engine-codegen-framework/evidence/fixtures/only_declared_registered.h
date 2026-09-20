#pragma once
#include "CodeGenMeta.h"
// Companion to only_declared.h: the registration channel makes the annotation list non-empty, so the tool
// really does search for the type and finds only a DECLARATION of it.
//
// The trailing semicolon is required: the macro body in CACore/header/CodeGenMeta.h ends with a
// static_assert and does NOT carry one, so a call site without it is not valid C++ (found by the round-33
// adversarial review; without it the fidelity gate refuses the header for the parse error, exit 6).
//
// With the semicolon: the cursor search notices that no definition was seen and refuses with exit 11
// before any table is written. That is why this case is recorded as a regression guard rather than as a
// red-green pair.
CA_CODEGEN_REGISTER("audit_ns::OnlyDeclared");
namespace audit_ns
{
    struct OnlyDeclared;
}
