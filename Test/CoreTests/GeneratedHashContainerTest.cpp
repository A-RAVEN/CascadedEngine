// codegen-container-semantics tasks 3.2 / 3.3: containers nested inside containers (and C arrays of
// containers) must produce the SAME bytes and the SAME hash for the same content regardless of the
// insertion order at ANY level; container elements must be hashed by their own specialization.
//
// Every claim below is printed with its two readings, and the "old path" reading is printed next to it:
// that expression (`h.hash(<container>)`) is exactly what the generated code used before this change,
// so the printed pair is the before/after of the same measurement, taken in one run.
#include <Serialization.h>
#include <CodeGenMeta.h>
#include <Hasher.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "TestContainer.h"
#include "Containers.Serialization.h"
#include "Containers.Hash.h"
#include "HasFloat.Hash.h"

namespace
{
    using codegen_containers::Containers;

    template<typename Obj>
    castl::vector<uint8_t> write(const Obj& o)
    {
        castl::vector<uint8_t> buffer;
        cacore::codegen::CodeGenSerializer<Obj>::serialize(o, buffer);
        return buffer;
    }

    bool sameBytes(const castl::vector<uint8_t>& a, const castl::vector<uint8_t>& b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }

    unsigned long long hashOf(const Containers& c)
    {
        cacore::aggregateHasher<> h;
        cacore::custom_hash_trait<Containers>::hash(c, h);
        return static_cast<unsigned long long>(h.getHash());
    }
} // namespace

bool RunGeneratedHashContainerTest()
{
    // ---- 3.2 (2): the SAME outer keys, a different insertion order INSIDE the nested containers.
    Containers a{};
    Containers b{};
    const unsigned innerA[] = { 5u, 1u, 9u };
    const unsigned innerB[] = { 9u, 5u, 1u };
    for (unsigned i = 0; i < 3; ++i)
    {
        a.umapOfUmap[1u].emplace(innerA[i], innerA[i]);
        b.umapOfUmap[1u].emplace(innerB[i], innerB[i]);
        // Same element content at the same position; only the insertion order INSIDE each element differs.
        a.vecOfUmap.push_back({ { 1u, 2u }, { 3u, 4u } });
        b.vecOfUmap.push_back({ { 3u, 4u }, { 1u, 2u } });
        a.nested.push_back({ static_cast<int>(innerA[i]) });
        b.nested.push_back({ static_cast<int>(innerA[i]) });
        a.umaps[0].emplace(innerA[i], innerA[i]);
        b.umaps[0].emplace(innerB[2 - i], innerB[2 - i]);
        a.umaps[1].emplace(innerA[i], innerA[i]);
        b.umaps[1].emplace(innerB[2 - i], innerB[2 - i]);
    }
    const bool nestedBytes = sameBytes(write(a), write(b));
    const bool nestedHash  = hashOf(a) == hashOf(b);
    const bool nestedEqual = cacore::custom_hash_trait<Containers>::equal(a, b);

    // The control: one different value deep inside must change both readings.
    Containers other = b;
    other.umapOfUmap[1u].emplace(77u, 77u);
    const bool nestedControl = !sameBytes(write(other), write(a)) && hashOf(other) != hashOf(a);

    // ---- 3.3: +0.0f and -0.0f are equal, so they must hash the same. The old expression did not.
    Containers p{};
    Containers n{};
    p.floatMap.emplace(1u, 0.0f);
    n.floatMap.emplace(1u, -0.0f);
    const bool floatEqual = cacore::custom_hash_trait<Containers>::equal(p, n);
    const bool floatHash  = hashOf(p) == hashOf(n);
    cacore::aggregateHasher<> oldPathA;
    cacore::aggregateHasher<> oldPathB;
    oldPathA.hash(p.floatMap);   // what the generated code used to emit
    oldPathB.hash(n.floatMap);
    const unsigned long long oldFloatA = static_cast<unsigned long long>(oldPathA.getHash());
    const unsigned long long oldFloatB = static_cast<unsigned long long>(oldPathB.getHash());

    // ---- 3.3: a trivially copyable element must still be hashed by ITS OWN specialization.
    Containers ep{};
    Containers en{};
    ep.floats.push_back(codegen_containers::HasFloat{ 0.0f });
    en.floats.push_back(codegen_containers::HasFloat{ -0.0f });
    const bool elementEqual = cacore::custom_hash_trait<Containers>::equal(ep, en);
    const bool elementHash  = hashOf(ep) == hashOf(en);
    cacore::aggregateHasher<> rawA;
    cacore::aggregateHasher<> rawB;
    rawA.hash(ep.floats[0]);     // what the generated code used to emit
    rawB.hash(en.floats[0]);
    const unsigned long long rawElementA = static_cast<unsigned long long>(rawA.getHash());
    const unsigned long long rawElementB = static_cast<unsigned long long>(rawB.getHash());

    std::printf("CodeGen nested bytes equal = %d, nested hash equal = %d, nested equal() = %d, control differs = %d\n",
                nestedBytes ? 1 : 0, nestedHash ? 1 : 0, nestedEqual ? 1 : 0, nestedControl ? 1 : 0);
    std::printf("CodeGen float map equal() = %d, new hash equal = %d | old path %016llx vs %016llx\n",
                floatEqual ? 1 : 0, floatHash ? 1 : 0, oldFloatA, oldFloatB);
    std::printf("CodeGen element trait equal() = %d, new hash equal = %d | raw bytes %016llx vs %016llx\n",
                elementEqual ? 1 : 0, elementHash ? 1 : 0, rawElementA, rawElementB);

    const bool ok = nestedBytes && nestedHash && nestedEqual && nestedControl &&
                    floatEqual && floatHash && elementEqual && elementHash;
    if (ok)
        std::puts("CodeGen nested container + element hash PASSED (task 3.2 / 3.3)");
    return ok;
}
