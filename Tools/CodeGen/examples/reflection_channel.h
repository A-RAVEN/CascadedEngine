// Example 3 — the CA_REFLECTION channel: a member list written outside the type, for a type you cannot
// annotate at all. It carries member NAMES only (no types, no offsets), so it supports
// `--emit-reflection` only; asking for serialization or hash is refused with exit code 8.
//
// A header that invokes a macro must include the header that DEFINES it — here CACore/header/Reflection.h,
// which is also where the engine defines it for the nine engine headers that use this channel. The tool
// must be given that include path: `-I CACore/header`.
#pragma once

#include "Reflection.h"

namespace game
{
    struct Stats
    {
        int   strength;
        int   agility;
        float critChance;
    };
}

CA_REFLECTION(game::Stats, strength, agility, critChance);
