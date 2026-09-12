//
// TestExternal.h — a type reflected ONLY via the CA_REFLECTION macro (the "unmigrated-engine +
// external" transition channel, design D2). Represents a library type you cannot add a marker to:
// CA_REFLECTION is declared at namespace/global scope and does NOT edit the type. The CodeGen tool
// reads this macro's member list from the source text (task 6b.3) and emits a fields-ONLY table.
//
#pragma once

struct CodeGenExternal
{
    int   a;
    float b;
};

CA_REFLECTION(CodeGenExternal, a, b);
