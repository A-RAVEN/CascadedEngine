#pragma once
#include "CodeGenMeta.h"
// Red case for the marker scan (found by the round-32 adversarial review): an earlier annotation's
// option list contains a STRING with an opening parenthesis in it, followed by a genuinely annotated
// struct.
//
// Measured on the 9.54-only binary (source blob 209e23b): the balanced-parenthesis collector started at
// the first marker, saw depth never return to zero (because the literal's parenthesis counted), gave up
// on EVERY marker, and refused the genuinely annotated struct with exit 9.
namespace audit_ns
{
    CA_REFLECT(name = "(")
    CA_REFLECT(serializable)
    struct ParenStr { int a; float b; };
}
