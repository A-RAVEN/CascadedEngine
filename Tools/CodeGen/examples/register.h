// Example 2 — the CA_CODEGEN_REGISTER channel: the type names itself in a string, and the type does NOT
// have to be edited, so this works for a type declared in a header you do not own.
//
// The trailing semicolon is required: the macro body ends with a static_assert and has no semicolon of
// its own (see CACore/header/CodeGenMeta.h).
#pragma once

#include "CodeGenMeta.h"

namespace game
{
    struct Inventory
    {
        int slots;
        int used;
    };
}

CA_CODEGEN_REGISTER("game::Inventory");
