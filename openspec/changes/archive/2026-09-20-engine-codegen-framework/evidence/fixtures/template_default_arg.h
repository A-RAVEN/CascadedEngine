#pragma once
#include "CodeGenMeta.h"

// Red case from the round-34 adversarial review (task 9.59): the annotation is followed by a class
// template whose default argument contains a nested-name-specifier.
//
// Measured on the 9.57 binary: the walk stopped at the `::`, named the introducer `Victim`, and handed the
// annotation to the UNMARKED namesake `Victim`, which then received a generated serializer.
namespace probe
{
    struct Victim { struct Tag { int t; }; int a; int b; };
    CA_REFLECT(serializable)
    template <class T = Victim::Tag> struct TemplArg { T v; };
}
