#pragma once
#include "CodeGenMeta.h"
// Red case for the marker scan: the annotation below spans three lines, and it is the FIRST
// annotation in this file.
//
// Both properties matter. A per-line scan that looks for the closing parenthesis on the SAME line
// reads the option list back empty, and the serialization gate then refuses a type that IS marked
// (measured on the 4091aa0 and 71c4413 binaries: exit 9). It must also be the first annotation here:
// in `r31-cases.h` the same three-line shape sits AFTER a marked type, where such a scan silently
// inherits the previous type's options (`serializable = false` leaks from Zero2 into Multi), so that
// file's `Multi` case passes on those binaries for the WRONG reason.
//
// The comment above deliberately does NOT spell the marker name with an unbalanced parenthesis; the
// separate `comment_paren.h` fixture covers that.
namespace audit_ns
{
    CA_REFLECT(
        serializable
    )
    struct ML { int a; float b; };
}
