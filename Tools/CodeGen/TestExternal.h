//
// TestExternal.h — a type reflected ONLY via the CA_REFLECTION macro (the "unmigrated-engine +
// external" transition channel, design D2). Represents a library type you cannot add a marker to:
// CA_REFLECTION is written at namespace/global scope and does NOT edit the type. The CodeGen tool
// reads this macro's member list from the invocation (task 6b.3) and emits a fields-ONLY table.
//
// The macro itself is defined by the engine in CACore/header/Reflection.h, and every one of the 22
// headers in this repository that uses it includes that definition first — a header that invokes a
// macro without defining it is not valid C++. Task 9.62: the tool now decides "is this type annotated?"
// from the macro invocations the preprocessor actually performed, so this self-contained fixture
// defines the same invocation shape locally instead of relying on a text search.
//
#pragma once

#define CA_REFLECTION(Type, ...) \
    static_assert(true, "CA_REFLECTION:" #Type)

struct CodeGenExternal
{
    int   a;
    float b;
};

CA_REFLECTION(CodeGenExternal, a, b);
