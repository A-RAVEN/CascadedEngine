#pragma once
#include "CodeGenMeta.h"

// Red case from the round-34 adversarial review (task 9.59): the annotation is followed by a bare USE of
// `struct Other` in a function declaration, then by the struct that really carries it.
//
// Measured on the 9.57 binary: the use was taken for a type introducer, it swallowed the annotation, and
// the annotated struct below was refused while `Other` was treated as the annotated type.
namespace probe
{
    struct Other { int q; };
    CA_REFLECT(serializable)
    void helper(struct Other* p);
    struct ElaboratedUse { int a; float b; };
}
