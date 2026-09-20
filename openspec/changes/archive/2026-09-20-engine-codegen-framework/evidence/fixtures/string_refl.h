#pragma once
#include "CodeGenMeta.h"
// Red case from the round-33 adversarial review (task 9.57): the annotation spelling for the OTHER channel
// (CA_REFLECTION) appears only inside a string literal, and the type carries no annotation at all.
//
// Measured on the 9.55 binary (blob 206a605): exit 0 and a reflection table with `fieldCount = 2` read out
// of the string, silently dropping the third real member `z`.
namespace audit_ns
{
    static const char* kNote = "CA_REFLECTION(audit_ns::StrRefl, x, y)";
    struct StrRefl { int x; double y; int z; };
}
