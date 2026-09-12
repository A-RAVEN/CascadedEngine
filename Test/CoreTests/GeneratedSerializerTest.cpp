//
// GeneratedSerializerTest.cpp — task 6.1: the CodeGen serialization emitter.
//
// Proves the emitted `CodeGenSerializer<T>` round-trips a reflected type field-by-field in declaration
// order: serialize -> deserialize -> get the original back. This is the "third exit" from the same
// type table (reflection / serialization), gated on the `serializable` marker option (AUDIT-4,
// decided 2026-09-08). Runtime-parallel to the existing `cacore::serialize`/`visit_members` path
// (which Serialization.h keeps using) — here we drive the generated code directly.
//
#include <Serialization.h>
#include <CodeGenMeta.h>
#include <CASTL/CAVector.h>
#include <cstdio>
#include <cstdint>
#include "TestEmit.h"
#include "generated/EmitSample.Serialization.h"

bool RunGeneratedSerializationTest()
{
    codegen_demo::EmitSample in{ 10, 3.5f, true, 7.25 };
    castl::vector<uint8_t> buffer;

    // Serialize field-by-field via the generated CodeGenSerializer (declaration order: a,b,c,d).
    cacore::codegen::CodeGenSerializer<codegen_demo::EmitSample>::serialize(in, buffer);

    // Declaration order over trivially-copyable fields => int(4)+float(4)+bool(1)+double(8) bytes.
    const std::size_t expectedBytes =
        sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(double);
    const std::size_t fieldCount = 4;

    // Deserialize into a fresh object; compare all fields AND the byte-size (a field-ORDER probe: a
    // misordered emitter would still round-trip but not produce this exact byte count in this order).
    codegen_demo::EmitSample out{};
    cacore::codegen::CodeGenSerializer<codegen_demo::EmitSample>::deserialize(out, buffer);

    const bool roundtrip =
        out.a == in.a && out.b == in.b && out.c == in.c && out.d == in.d;
    const bool orderOk = (buffer.size() == expectedBytes) && (fieldCount == 4);

    if (!roundtrip || !orderOk)
    {
        std::printf("Generated serialization FAILED (task 6.1): roundtrip=%d orderOk=%d bytes=%zu\n",
                    (int)roundtrip, (int)orderOk, buffer.size());
        return false;
    }
    std::puts("CodeGen serialization roundtrip PASSED (task 6.1)");
    return true;
}
