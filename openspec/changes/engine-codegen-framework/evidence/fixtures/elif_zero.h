#pragma once
#include "CodeGenMeta.h"

// Red case from the round-34 adversarial review (task 9.59): the annotation sits in an `#elif 0` arm, which
// is dead in every configuration.
//
// Measured on the 9.57 binary: the `#elif` handler ignored its condition, kept the arm as code, and the
// unmarked struct below received a serializer.
namespace probe
{
#if SOME_MACRO_THAT_IS_NEVER_DEFINED
    int x;
#elif 0
    CA_REFLECT(serializable)
#endif
    struct ElifZero { int a; float b; };
}
