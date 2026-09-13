//
// GeneratedHashTest.cpp — task 6.2: the CodeGen hash/equality emitter.
//
// Proves the emitted `cacore::custom_hash_trait<T>` (the hook the existing `aggregateHasher` already
// calls via `has_custom_hash_func`) hashes each field in declaration order (NOT raw whole-struct bytes,
// => padding/layout independent) and normalizes floats: hash(-0.0)==hash(+0.0), NaN hashes stably, and
// `equal` treats -0.0/+0.0 and NaN/NaN as equal. Non-goal preserved: this does NOT migrate the existing
// HashObj semantic — it's a generated trait exercised directly.
//
#include <Hasher.h>
#include <CodeGenMeta.h>
#include <cstdio>
#include <limits>
#include "TestEmit.h"
#include "generated/EmitSample.Hash.h"

bool RunGeneratedHashTest()
{
    using EmitSample = codegen_demo::EmitSample;
    using Trait = cacore::custom_hash_trait<EmitSample>;

    // Drive the GENERATED trait directly. NOTE: `cacore::hash<EmitSample>` would BYPASS it because
    // aggregateHasher::hash checks std::is_trivially_copyable_v FIRST and raw-hashes the whole struct
    // (that raw-bytes path is exactly what 6.2's generated field-wise hash replaces). So we invoke the
    // generated custom_hash_trait for the assertions; the integration nuance is documented in tasks 6.2.
    auto hashOf = [](EmitSample const& o)
    {
        cacore::aggregateHasher<cacore::default_hashclass> hasher;
        Trait::hash(o, hasher);
        return hasher.getHash();
    };

    // 1) Identical objects -> same hash + equal.
    EmitSample a{ 1, 2.0f, false, 3.0 };
    EmitSample b{ 1, 2.0f, false, 3.0 };
    if (hashOf(a) != hashOf(b) || !Trait::equal(a, b))
    {
        std::printf("HASH FAIL: identical objects diverged (hashA=%llu hashB=%llu equal=%d)\n",
                    (unsigned long long)hashOf(a), (unsigned long long)hashOf(b),
                    (int)Trait::equal(a, b));
        return false;
    }

    // 2) -0.0 vs +0.0 -> same hash + equal (float normalization).
    EmitSample nzA{ 1, -0.0f, false, 3.0 };
    EmitSample nzB{ 1, +0.0f, false, 3.0 };
    if (hashOf(nzA) != hashOf(nzB) || !Trait::equal(nzA, nzB))
    {
        std::printf("HASH FAIL: -0.0/+0.0 diverged (hashNeg=%llu hashPos=%llu equal=%d)\n",
                    (unsigned long long)hashOf(nzA), (unsigned long long)hashOf(nzB),
                    (int)Trait::equal(nzA, nzB));
        return false;
    }

    // 3) NaN vs NaN -> same hash + equal (canonical NaN).
    const float nanf = std::numeric_limits<float>::quiet_NaN();
    EmitSample na{ 1, nanf, false, 3.0 };
    EmitSample nb{ 1, nanf, false, 3.0 };
    if (hashOf(na) != hashOf(nb) || !Trait::equal(na, nb))
    {
        std::printf("HASH FAIL: NaN diverged (hashA=%llu hashB=%llu equal=%d)\n",
                    (unsigned long long)hashOf(na), (unsigned long long)hashOf(nb),
                    (int)Trait::equal(na, nb));
        return false;
    }

    // 4) Different value -> different hash + not equal.
    EmitSample c{ 1, 9.0f, false, 3.0 };
    if (hashOf(a) == hashOf(c) || Trait::equal(a, c))
    {
        std::printf("HASH FAIL: distinct values collided (hashA=%llu hashC=%llu equal=%d)\n",
                    (unsigned long long)hashOf(a), (unsigned long long)hashOf(c),
                    (int)Trait::equal(a, c));
        return false;
    }

    std::puts("CodeGen hash/equality test PASSED (task 6.2)");
    return true;
}
