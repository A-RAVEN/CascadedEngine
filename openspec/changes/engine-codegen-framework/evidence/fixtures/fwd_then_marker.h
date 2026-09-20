#pragma once
#include "CodeGenMeta.h"
namespace audit_ns { struct Same; }
namespace audit_ns
{
    CA_REFLECT(serializable)
    struct Same { int a; float b; };
}
