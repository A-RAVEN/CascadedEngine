//
// GeneratedSerializerTest.cpp — the CodeGen serialization emitter.
//
// Proves the emitted `CodeGenSerializer<T>` round-trips a reflected type field-by-field in declaration
// order: serialize -> deserialize -> get the original back. This is the "third exit" from the same
// type table (reflection / serialization), gated on the `serializable` marker option. It drives the
// generated code directly, on the CodeGen-owned runtime.
//
#include <CodeGenMeta.h>
#include <CASTL/CAVector.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "TestEmit.h"
#include "EmitSample.codegen.h"

bool RunGeneratedSerializationTest()
{
    codegen_demo::EmitSample in{ 10, 3.5f, true, 7.25 };
    castl::vector<uint8_t> buffer;

    // Serialize field-by-field via the generated CodeGenSerializer (declaration order: a,b,c,d).
    cacore::codegen::CodeGenSerializer<codegen_demo::EmitSample>::serialize(in, buffer);

    // The field-ORDER probe must be anchored to BYTE POSITIONS. The previous version computed
    // `expectedBytes` from the same sizes it was checking and then compared a local constant with 4 —
    // both halves are true for ANY field order (a reversed emitter produces the same 17 bytes), so the
    // "order" claim was unfalsifiable.
    const std::size_t expectedBytes = 4 + 4 + 1 + 8;   // a,b,c,d in declaration order

    // Deserialize into a fresh object; compare all fields AND each field's BYTES AT ITS OWN OFFSET.
    // The read position is an explicit parameter.
    codegen_demo::EmitSample out{};
    std::size_t position = 0;
    const bool readOk = cacore::codegen::CodeGenSerializer<codegen_demo::EmitSample>::deserialize(out, buffer, position);

    const bool roundtrip =
        readOk && position == buffer.size() &&
        out.a == in.a && out.b == in.b && out.c == in.c && out.d == in.d;

    bool orderOk = (buffer.size() == expectedBytes);
    if (orderOk)
    {
        int    atA = 0; float atB = 0.0f; bool atC = false; double atD = 0.0;
        std::memcpy(&atA, buffer.data() + 0, sizeof(atA));
        std::memcpy(&atB, buffer.data() + 4, sizeof(atB));
        std::memcpy(&atC, buffer.data() + 8, sizeof(atC));
        std::memcpy(&atD, buffer.data() + 9, sizeof(atD));
        orderOk = atA == in.a && atB == in.b && atC == in.c && atD == in.d;
        if (!orderOk)
            std::printf("Generated serialization ORDER FAILED: at bytes 0/4/8/9 got "
                        "%d/%g/%d/%g, expected %d/%g/%d/%g\n",
                        atA, (double)atB, (int)atC, atD, in.a, (double)in.b, (int)in.c, in.d);
    }

    if (!roundtrip || !orderOk)
    {
        std::printf("Generated serialization FAILED: roundtrip=%d orderOk=%d bytes=%zu\n",
                    (int)roundtrip, (int)orderOk, buffer.size());
        return false;
    }
    std::puts("CodeGen serialization roundtrip PASSED");
    return true;
}

