#pragma once
#include "CodeGenMeta.h"

// Red case from the round-34 adversarial review (task 9.59): the function name contains the keyword, and
// the annotation above it belongs to the struct below.
//
// Measured on the 9.57 binary: the keyword match was not token-bounded on the right, the introducer was
// named "ify", it consumed the annotation, and the struct that really carries it was refused.
namespace probe
{
    CA_REFLECT(serializable)
    inline int classify(int x) { return x; }
    struct ClassifyId { int a; float b; };
}
