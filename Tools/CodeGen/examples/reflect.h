// Example 1 — the CA_REFLECT channel: the marker sits directly above the type it annotates.
//
// Copy this file and replace the type. It supports all three emitters, because the marker carries the
// `serializable` option (and a `min` option the serialization emitter reads back as text).
#pragma once

#include "CodeGenMeta.h"   // the descriptor/serializer interface the generated code depends on

namespace game
{
    CA_REFLECT(serializable, min = 0)
    struct Player
    {
        int    id;
        float  health;
    };
}
