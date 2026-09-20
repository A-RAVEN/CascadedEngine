#pragma once
#include "CodeGenMeta.h"
// Red case for the marker scan: a comment mentions the marker spelling followed by an opening
// parenthesis, and that parenthesis is never closed inside the comment. The struct below IS properly
// annotated on one line, so it must be accepted.
//
// Measured on the caa8643 binary before the fix: the balanced-parenthesis collector started at the
// occurrence inside this comment, ran to the end of the file without ever balancing, and gave up
// entirely -- so the real annotation was never collected, the option list read back empty, and the
// tool refused a type that is annotated (exit 9). After the fix the comment bytes are blanked out
// first, so only the real annotation is collected.
//
// The unbalancing sample is the literal below, written inside a comment on purpose:
//   CA_REFLECT(
namespace audit_ns
{
    CA_REFLECT(serializable)
    struct Paren { int a; float b; };
}
