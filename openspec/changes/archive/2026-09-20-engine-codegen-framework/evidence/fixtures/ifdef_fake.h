#pragma once
#include "CodeGenMeta.h"
// Companion of ifzero.h: the disabled branch is guarded by a name that is defined nowhere (not in this
// file, not on the command line), so the branch is inactive too.
namespace probe
{
#ifdef NEVER_DEFINED
    CA_REFLECT(serializable)
#endif
    struct IfDef { int a; float b; };
}
