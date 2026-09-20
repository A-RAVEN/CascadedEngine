#pragma once
#include "CodeGenMeta.h"
// Red case from the round-33 adversarial review (task 9.57): an attribute or an export macro between the
// keyword and the type name made the tool take THAT identifier as the type name.
//
// Measured on the 9.55 binary (blob 206a605): `Aligned` -> exit 4 "is not annotated" and the phantom name
// `alignas` / `CA_FAKE_API` ended up in the annotation list.
#define CA_FAKE_API
namespace probe
{
    CA_REFLECT(serializable)
    struct alignas(8) Aligned { int a; float b; };
    CA_REFLECT(serializable)
    struct CA_FAKE_API Exported { int a; float b; };
}
