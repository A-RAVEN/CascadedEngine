#pragma once
#include "CodeGenMeta.h"
// Red case from the round-33 adversarial review (task 9.57): the annotation belongs to a type that is only
// DECLARED. The first binder skipped declarations entirely, so the annotation travelled past it to the
// next definition, and the UNMARKED type below received the generated serializer.
//
// Measured on the 9.55 binary (blob 206a605): `Victim` -> exit 0 with `CodeGenSerializer<audit_ns::Victim>`,
// while `Declared` (the annotated one) -> exit 4 "is not annotated".
namespace audit_ns
{
    CA_REFLECT(serializable)
    struct Declared;

    struct Victim { int a; float b; };
}
