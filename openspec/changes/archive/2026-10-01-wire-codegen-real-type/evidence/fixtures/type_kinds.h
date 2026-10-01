#pragma once
// Task 1.3 fixture: the same four members written two ways. Used with a TEMPORARY [DIAGDUMP] inside
// the tool to prove WHY `uint32_t` lands in TypeKind::Opaque while `unsigned int` does not: the field
// collector stores `clang_getCursorType(cursor).kind` without canonicalizing, so a typedef/elaborated
// spelling reports CXType_Elaborated (119) instead of CXType_UInt (106).
#include "CodeGenMeta.h"

namespace probe
{
    CA_REFLECT(serializable)
    struct TypeKinds
    {
        unsigned int       spelled_uint;
        uint32_t           typedef_uint;
        unsigned long long spelled_ull;
        uint64_t           typedef_ull;
    };
}
