//
// TestStruct.h — self-contained test input for the CodeGen proof-of-pipeline (task 3.1/3.2/6b).
// Includes CodeGenMeta.h so a CA_REFLECT marker macro is defined for libclang's preprocessor pass.
// The CodeGen tool extracts `CodeGenSample`'s fields into a deterministic type table, and (task 6b.2)
// reads the CA_REFLECT options from this source text.
//
#pragma once

#include "CodeGenMeta.h"

namespace codegen_test
{
    CA_REFLECT(serializable, min = 0)
    struct CodeGenSample
    {
        int   a;
        float b;
        bool  c;
        double d;
    };
} // namespace codegen_test
