//
// TestExternal.h — a type reflected ONLY via the CA_REFLECTION macro (the "unmigrated-engine +
// external" transition channel, design D2). Represents a library type you cannot add a marker to:
// CA_REFLECTION is written at namespace/global scope and does NOT edit the type. The CodeGen tool
// reads this macro's member list from the invocation (task 6b.3) and emits a fields-ONLY table.
//
// REAL USAGE, not a workaround (corrected 2026-09-20): a header that invokes a macro must include the
// DEFINES it — all nine engine headers that use CA_REFLECTION include CACore/header/Reflection.h first.
// The tool decides "is this type annotated?" from the macro invocations the preprocessor actually
// performed, so this fixture does exactly what a real user does. It used to define the macro
// locally, which made the documentation look as if every user had to hand-roll a fallback definition.
//
#pragma once

#include "Reflection.h"

struct CodeGenExternal
{
    int   a;
    float b;
};

CA_REFLECTION(CodeGenExternal, a, b);
