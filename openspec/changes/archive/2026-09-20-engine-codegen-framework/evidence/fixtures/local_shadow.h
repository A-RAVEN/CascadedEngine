#pragma once
#include "CodeGenMeta.h"
// Red case from the round-33 adversarial review (task 9.57): a struct declared inside a function was
// pooled with the namespace-level type of the same name, because the cursor visitor recurses into
// functions and the target was matched by unqualified name.
//
// Measured on the 9.55 binary (blob 206a605): `Foo` -> the field table read `Foo 1 / field inner`, i.e.
// the member list of the function-local struct, not of the annotated namespace-level one.
namespace probe
{
    inline void helper() { struct Foo { int inner; }; (void)sizeof(Foo); }

    CA_REFLECT(serializable)
    struct Foo { int a; int b; };
}
