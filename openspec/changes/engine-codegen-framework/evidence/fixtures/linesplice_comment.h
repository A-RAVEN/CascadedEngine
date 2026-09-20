#pragma once
#include "CodeGenMeta.h"
// Red case for the marker scan (found by the round-32 adversarial review): the line comment above the
// struct ends with a BACKSLASH, so C++ translation phase 2 splices it with the next physical line and
// that next line is STILL comment text. The commented-out annotation below therefore is not an
// annotation, and the type below carries none.
//
// Measured on the 9.54-only binary (source blob 209e23b): the tool read the spliced line as real code,
// treated the type as annotated, wrote a serialization specialization for it and exited 0. MSVC's own
// preprocessor on the same text emits warning C4010 "single-line comment contains line-continuation
// character" and produces no marker at all.
namespace audit_ns
{
    // the annotation is inside this comment and the comment continues \
    CA_REFLECT(serializable)
    struct Spliced { int a; float b; };
}
