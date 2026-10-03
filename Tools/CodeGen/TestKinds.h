#pragma once
// Task 1.1 fixture: the same four members written two ways. Before the classifier canonicalized the
// type, `uint32_t` / `uint64_t` came out as CXType_Elaborated (119) and landed in TypeKind::Opaque,
// while the keyword spellings were classified correctly. The CoreTests assertions pinned to this
// header are what makes that difference fail the build.
#include "CodeGenMeta.h"

namespace codegen_kinds
{
    CA_REFLECT(serializable)
    struct Kinds
    {
        unsigned int       spelled_uint;
        uint32_t           typedef_uint;
        unsigned long long spelled_ull;
        uint64_t           typedef_ull;
    };
}
