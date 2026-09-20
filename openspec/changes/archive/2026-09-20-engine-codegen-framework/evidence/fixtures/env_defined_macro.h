#pragma once
#include "CodeGenMeta.h"

// Red case from the round-34 adversarial review (task 9.59): the annotation sits behind a macro the
// COMPILER defines for this target, which the tool never passes on its own command line.
//
// Measured on the 9.57 binary: `#ifdef` was decided against the tool's own -D list only, so the arm was
// declared dead, the genuinely annotated type was dropped, and the same code written as
// `#if defined(_WIN32)` was kept - two contradictory answers inside one invocation.
namespace probe
{
#ifdef _WIN32
    CA_REFLECT(serializable)
#endif
    struct EnvMacro { int a; float b; };
}
