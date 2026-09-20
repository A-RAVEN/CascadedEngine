#pragma once
#include "CodeGenMeta.h"
namespace audit_ns { struct Same; }          // forward declaration FIRST (9.42 red case)
namespace audit_ns
{
    CA_REFLECT(serializable)
    struct Same { int a; float b; };
}
