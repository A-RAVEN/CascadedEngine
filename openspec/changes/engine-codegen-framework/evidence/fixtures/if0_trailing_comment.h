#pragma once
#include "CodeGenMeta.h"

// Red case from the round-34 adversarial review (task 9.59): identical to ifzero.h except that the
// directive carries a trailing comment. The text decision compared the whole remainder of the line with
// "0", so this spelling was treated as undecidable and the dead branch stayed code: the unmarked struct
// below received a generated serializer (measured on the 9.57 binary).
namespace probe
{
#if 0  // disabled for now
    CA_REFLECT(serializable)
#endif
    struct If0Comment { int a; float b; };
}
