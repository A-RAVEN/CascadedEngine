//
// TestEmit.h — the *emitter* demo type for task 5.1 (reflection emitter).
//
// Unlike the 6b test headers, this type is meant to prove the reflection EMITTER end-to-end:
// it carries ONLY the CA_REFLECT marker (no CA_REFLECTION member list), so the CodeGen tool takes
// the libclang AST path and gets the FULL metadata (name / type / offset / semantic kind) — exactly
// what the author would NOT want to hand-write. The tool's --emit-reflection mode produces a
// TypeDescriptor<codegen_demo::EmitSample> specialization from that table (the emitter's main job).
//
// The emitted code is a FRAGMENT: the consumer TU must already have the type definition + CodeGenMeta.h
// on the include path. In a real engine build that's guaranteed by build.py feeding the type header
// along with the generated file; the ABI guard (static_assert_abi_consistency) then confirms the
// generated fieldCount agrees with the source-of-truth reflection.
//
#pragma once

#include "CodeGenMeta.h"

namespace codegen_demo
{
    CA_REFLECT(serializable, min = 0)
    struct EmitSample
    {
        int    a;
        float  b;
        bool   c;
        double d;
    };
} // namespace codegen_demo
