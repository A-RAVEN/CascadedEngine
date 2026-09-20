#pragma once
#include "CodeGenMeta.h"
// Red case from the round-33 adversarial review (task 9.57): the only annotation spelling in this file
// sits inside a preprocessor-disabled branch, and the type below carries none.
//
// Measured on the 9.55 binary (blob 206a605): exit 0 and a full serialization specialization for
// `Evil`, i.e. an unmarked type shipped with generated (de)serialization.
namespace probe
{
#if 0
    CA_REFLECT(serializable)
    struct Evil { int deadA; int deadB; };
#endif
    struct Evil { int a; float b; };
}
