#pragma once
#include "CodeGenMeta.h"
namespace audit_ns
{
    CA_REFLECT(serializable = 0)
    struct Zero1 { int a; };
    CA_REFLECT(serializable = false)
    struct Zero2 { int a; };
    CA_REFLECT(
        serializable
    )
    struct Multi { int a; float b; };
    CA_REFLECT(serializable)
    struct D0 { int a; };
    struct D1 { int a; };
}
