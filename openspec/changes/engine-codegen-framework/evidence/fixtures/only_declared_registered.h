#pragma once
#include "CodeGenMeta.h"
// Companion to only_declared.h: the registration channel makes the whitelist non-empty, so the tool
// really does search for the type and finds only a DECLARATION of it.
//
// Measured on the 9.54-only binary (source blob 209e23b): exit 6 (the fidelity gate) rather than a
// report about the missing definition. On the fixed binary the cursor search notices that no definition
// was seen and refuses with exit 11 before any table is written. Both are refusals, which is why this
// case is recorded as a regression guard rather than as a red-green pair.
CA_CODEGEN_REGISTER("audit_ns::OnlyDeclared")
namespace audit_ns
{
    struct OnlyDeclared;
}
