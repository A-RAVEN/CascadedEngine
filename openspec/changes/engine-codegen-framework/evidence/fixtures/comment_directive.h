#pragma once
#include "CodeGenMeta.h"

// Red case from the round-34 adversarial review (task 9.59): the comment below CONTAINS a directive, and
// the real annotation after it must still be honoured.
//
// Measured on the 9.57 binary: the directive text inside the comment was executed, the rest of the file
// was blanked as a disabled branch, and the correctly annotated type produced no output at all while the
// tool reported success (exit 0, "no annotated types").
namespace probe
{
/* the previous version of this header was:
#if 0
*/
    CA_REFLECT(serializable)
    struct CommentDirective { int a; float b; };
}
