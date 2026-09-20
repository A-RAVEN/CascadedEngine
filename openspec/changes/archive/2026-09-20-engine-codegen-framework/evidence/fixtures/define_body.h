#pragma once
#include "CodeGenMeta.h"

// Red case from the round-34 adversarial review (task 9.59): the marker spelling occurs in the body of a
// macro that is NEVER USED, above an unmarked struct.
//
// Measured on the 9.57 binary: the definition body was read as a real annotation and the unmarked struct
// was given a serializer (exit 0, artifact written).
namespace probe
{
#define ANNOTATE CA_REFLECT(serializable)
    struct DefineBody { int a; float b; };
}
