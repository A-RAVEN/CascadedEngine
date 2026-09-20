#pragma once
#include "CodeGenMeta.h"
// Red case from the round-33 adversarial review (task 9.57): the `class` of `enum class` was mistaken for
// a type introducer, so the annotation bound to the ENUM and the struct that really carries it was refused.
//
// Measured on the 9.55 binary (blob 206a605): `Key` -> exit 4 "is not annotated".
namespace probe
{
    CA_REFLECT(serializable)
    enum class Color { Red };
    struct Key { int a; float b; };
}
