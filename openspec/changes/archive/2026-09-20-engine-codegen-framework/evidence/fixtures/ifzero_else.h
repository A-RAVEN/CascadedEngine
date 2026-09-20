#pragma once
#include "CodeGenMeta.h"
// Control for the conditional blanker: the DISABLED branch must be ignored, the ELSE branch is active and
// its annotation must still be honoured (a blanker that dropped the whole chain would fail here).
namespace probe
{
#if 0
    CA_REFLECT(ui)
#else
    CA_REFLECT(serializable)
#endif
    struct ElseOk { int a; float b; };
}
