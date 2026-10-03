// codegen-container-semantics tasks 3.1 / 3.4: the generated container code must produce the SAME bytes
// for the same content regardless of insertion history, must differ when the content differs, and must
// read back into a container whose old elements were cleared first.
#include <Serialization.h>
#include <CodeGenMeta.h>
#include <CASTL/CAVector.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "TestContainer.h"
#include "Containers.Serialization.h"

namespace
{
    using codegen_containers::Containers;

    void fill(Containers& c, const unsigned* umapKeys, unsigned umapCount,
              const unsigned* usetKeys, unsigned usetCount)
    {
        for (unsigned i = 0; i < umapCount; ++i)
            c.umap.emplace(umapKeys[i], umapKeys[i] * 10u);
        for (unsigned i = 0; i < usetCount; ++i)
            c.uset.insert(usetKeys[i]);
        c.vec.push_back(1u);
        c.vec.push_back(2u);
        c.omap.emplace(4u, 40u);
        c.oset.insert(6u);
        c.plain = 42u;
    c.label = "ordered";
    }

    castl::vector<uint8_t> write(const Containers& c)
    {
        castl::vector<uint8_t> buffer;
        cacore::codegen::CodeGenSerializer<Containers>::serialize(c, buffer);
        return buffer;
    }

    bool sameBytes(const castl::vector<uint8_t>& a, const castl::vector<uint8_t>& b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }
} // namespace

bool RunGeneratedContainerTest()
{
    const unsigned kA[] = { 5u, 1u, 9u, 3u };
    const unsigned kB[] = { 3u, 9u, 1u, 5u };   // same content, different insertion order
    const unsigned sA[] = { 7u, 2u, 8u };
    const unsigned sB[] = { 8u, 7u, 2u };

    Containers a{};
    Containers b{};
    fill(a, kA, 4, sA, 3);
    fill(b, kB, 4, sB, 3);

    const castl::vector<uint8_t> ba = write(a);
    const castl::vector<uint8_t> bb = write(b);
    const bool stableBytes = sameBytes(ba, bb);

    // The control: one extra entry must change the bytes — otherwise "stable" would be vacuous.
    Containers other{};
    fill(other, kA, 4, sA, 3);
    other.umap.emplace(100u, 1u);
    const bool controlDiffers = !sameBytes(write(other), ba);

    // Task 3.4: read into a container that already holds elements; the generated code clears first, so
    // the result is decided by the payload alone (no 999 left behind).
    Containers back{};
    back.umap.emplace(999u, 999u);
    back.uset.insert(999u);
    back.vec.push_back(999u);
    cacore::codegen::CodeGenSerializer<Containers>::deserialize(back, ba);
    const bool readClean = back.umap.size() == a.umap.size() && back.umap.count(999u) == 0 &&
                           back.uset.size() == a.uset.size() && back.uset.count(999u) == 0 &&
                           back.vec.size() == a.vec.size() && back.umap.count(1u) == 1;

    // Task 3.4 clause 2: the OLD expression (what the generated code emitted before this change) reads
    // straight into the target and never clears it — the two readings below are the failing state.
    Containers legacy{};
    legacy.vec.push_back(999u);
    legacy.vec.push_back(999u);
    legacy.vec.push_back(999u);
    {
        cacore::deserializer<castl::vector<uint8_t>> d(ba);
        d.deserialize(legacy.vec);
    }
    Containers emptyPayload{};
    const castl::vector<uint8_t> be = write(emptyPayload);
    Containers legacyEmpty{};
    legacyEmpty.vec.push_back(999u);
    {
        cacore::deserializer<castl::vector<uint8_t>> d(be);
        d.deserialize(legacyEmpty.vec);
    }
    const bool oldPathKeeps = legacy.vec.size() != a.vec.size() && legacy.vec[0] == 999u;
    const bool oldPathEmpty = legacyEmpty.vec.size() == 1u;

    // Task 3.5: the ordered members (vector / map / string) must produce the same bytes as before this
    // change. "Before" = the expression the generated code used to emit (`s.serialize(obj.x)` straight
    // into the old runtime), evaluated here on the SAME object in the SAME run.
    const auto oldPath = [](const auto& member) {
        castl::vector<uint8_t> buffer;
        cacore::serializer<castl::vector<uint8_t>> s(buffer);
        s.serialize(member);
        return buffer;
    };
    const auto newPath = [](const auto& member) {
        castl::vector<uint8_t> buffer;
        cacore::serializer<castl::vector<uint8_t>> s(buffer);
        cacore::codegen::cgWriteContainer(s, member);
        return buffer;
    };
    const bool orderedSame = sameBytes(oldPath(a.vec), newPath(a.vec)) &&
                             sameBytes(oldPath(a.omap), newPath(a.omap)) &&
                             sameBytes(oldPath(a.label), newPath(a.label));

    std::printf("CodeGen container bytes stable = %d (bytes=%zu)\n", stableBytes ? 1 : 0, ba.size());
    std::printf("CodeGen container control differs = %d\n", controlDiffers ? 1 : 0);
    std::printf("CodeGen container read-back clean = %d\n", readClean ? 1 : 0);
    std::printf("CodeGen ordered members match the old path = %d (vector/map/string)\n", orderedSame ? 1 : 0);
    std::printf("CodeGen old read path keeps stale elements: size=%zu (was %zu), empty payload into 1 element = %zu\n",
                legacy.vec.size(), a.vec.size(), legacyEmpty.vec.size());
    if (stableBytes && controlDiffers && readClean && oldPathKeeps && oldPathEmpty && orderedSame)
        std::puts("CodeGen container serialization PASSED (task 3.1 / 3.4)");
    return stableBytes && controlDiffers && readClean && oldPathKeeps && oldPathEmpty && orderedSame;
}
