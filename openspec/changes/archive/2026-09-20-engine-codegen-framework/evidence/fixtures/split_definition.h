#pragma once
#include "CodeGenMeta.h"
// Red case found by the round-32 adversarial review (task 9.55): one annotated type, and then a second
// type that carries NO annotation of its own. The first type's definition is written with the keyword,
// the name and the body brace on three separate lines.
//
// Measured on the 9.54-only binary (source blob 209e23b): the "does this line start another type's
// definition?" test was a same-line text heuristic, so the three-line definition consumed nothing, the
// marker leaked past it, and the UNANNOTATED second type was given a serialization specialization
// (exit 0). Writing the first definition on one line made the same file refuse correctly (exit 9).
namespace audit_ns
{
    CA_REFLECT(serializable)
    struct
    Split0
    { int a; };

    struct Split1 { int b; };
}
