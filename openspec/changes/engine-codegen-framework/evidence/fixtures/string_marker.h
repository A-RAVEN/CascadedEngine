#pragma once
#include "CodeGenMeta.h"
// Red case for the marker scan (found by the round-32 adversarial review): the only place the marker
// spelling occurs is inside a STRING LITERAL. The struct below carries no annotation.
//
// Measured on the 9.54-only binary (source blob 209e23b): the literal was read as a real annotation and
// a serialization specialization was written for the unannotated struct, exit 0.
namespace audit_ns
{
    inline const char* helpText =
        "write the annotation spelling here, followed by an opening parenthesis: ";
    inline const char* example =
        "CA_REFLECT(serializable)";
    struct FromString { int a; float b; };
}
