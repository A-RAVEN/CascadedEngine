#pragma once
#include "CodeGenMeta.h"
// Regression case, the mirror image of string_marker.h: a STRING LITERAL contains the block-comment
// opener, and the struct below carries a REAL annotation.
//
// Measured on the 9.54-only binary (source blob 209e23b): the comment blanker treated the opener inside
// the literal as the start of a block comment that never closes, blanked the rest of the file, and the
// genuinely annotated type came out as an empty annotation list (exit 0, no output). That was a
// regression against the binary from before the 9.54 fix, which emitted the specialization.
namespace audit_ns
{
    inline const char* openBlockComment = "/*";
    CA_REFLECT(serializable)
    struct StrBlock { int a; float b; };
}
