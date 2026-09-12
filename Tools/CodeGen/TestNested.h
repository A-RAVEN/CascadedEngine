//
// TestNested.h — proves the nested-member semantic (task 6b.4): a struct-typed member counts as ONE
// top-level field (the tool does NOT recurse/flatten into it). Outer has 2 top-level fields:
// `n` (a CodeGenNested struct, treated as one field) and `y`. The tool reports fieldCount == 2, with
// n at offset 0 and y at its own offset.
//
#pragma once

#include "CodeGenMeta.h"

namespace codegen_test
{
    struct CodeGenNested
    {
        int x;
    };

    CA_REFLECT(serializable)
    struct CodeGenOuter
    {
        CodeGenNested n;
        int           y;
    };
} // namespace codegen_test
