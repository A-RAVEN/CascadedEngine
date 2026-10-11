// The generated container code must produce the SAME bytes
// for the same content regardless of insertion history, must differ when the content differs, and must
// read back into a container whose old elements were cleared first.
#include <CodeGenMeta.h>
#include <CASTL/CAVector.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "TestContainer.h"
#include "Containers.codegen.h"
// 容器里装结构体（`vector<HasFloat>` 与 `vector<Inner>`）：元素的读写由它自己的生成代码接手，
// 所以这两个元素的生成物（各自一份 `<类型>.codegen.h`）必须在这里可见。
#include "HasFloat.codegen.h"
#include "Inner.codegen.h"
#include "Outer.codegen.h"

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

    // 容器里装结构体（Outer 的元素 Inner 内部还有无序容器）。
    template<typename T>
    castl::vector<uint8_t> writeAny(const T& object)
    {
        castl::vector<uint8_t> buffer;
        cacore::codegen::CodeGenSerializer<T>::serialize(object, buffer);
        return buffer;
    }

    bool sameBytes(const castl::vector<uint8_t>& a, const castl::vector<uint8_t>& b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }

    // 十六进制文本 → 字节（期望值写成十六进制，见下面 `kOldVecHex` 那几行）。
    castl::vector<uint8_t> hexToBytes(const char* hex)
    {
        castl::vector<uint8_t> out;
        int high = -1;
        for (const char* p = hex; *p != '\0'; ++p)
        {
            const char c = *p;
            int value = -1;
            if (c >= '0' && c <= '9')
                value = c - '0';
            else if (c >= 'a' && c <= 'f')
                value = c - 'a' + 10;
            else
                continue;
            if (high < 0)
                high = value;
            else
            {
                out.push_back(static_cast<uint8_t>((high << 4) | value));
                high = -1;
            }
        }
        return out;
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

    // Read into a container that already holds elements; the generated code clears first, so
    // the result is decided by the payload alone (no 999 left behind).
    Containers back{};
    back.umap.emplace(999u, 999u);
    back.uset.insert(999u);
    back.vec.push_back(999u);
    {
        std::size_t position = 0;
        cacore::codegen::CodeGenSerializer<Containers>::deserialize(back, ba, position);
    }
    const bool readClean = back.umap.size() == a.umap.size() && back.umap.count(999u) == 0 &&
                           back.uset.size() == a.uset.size() && back.uset.count(999u) == 0 &&
                           back.vec.size() == a.vec.size() && back.umap.count(1u) == 1;

    // The OLD read path (read straight into the target, never clear it) is gone with the old runtime:
    // it used to be demonstrated right here (`size=5 (was 2)`, empty payload into 1 element) and that
    // reading is kept in the change's artifacts. What must hold now is the line above (`readClean`).
    const auto newPath = [](const auto& member) {
        castl::vector<uint8_t> buffer;
        cacore::codegen::writer<castl::vector<uint8_t>> w(buffer);
        cacore::codegen::cgWriteContainer(w, member);
        return buffer;
    };

    // The ordered members (vector / map / string) must produce the same bytes as before this
    // change. "Before" = the old runtime's own bytes for the SAME object, measured once on
    // 2026-10-05 and written down here as expectations (the old headers are gone now):
    //   vector{1,2} → 长度 2 + 两个元素；map{4:40} → 长度 1 + 键值对；string "ordered" → 长度 + 内容。
    const char* const kOldVecHex = "02000000000000000100000002000000";
    const char* const kOldOMapHex = "01000000000000000400000028000000";
    const char* const kOldLabelHex = "07000000000000006f726465726564";
    const bool orderedSame = sameBytes(hexToBytes(kOldVecHex), newPath(a.vec)) &&
                             sameBytes(hexToBytes(kOldOMapHex), newPath(a.omap)) &&
                             sameBytes(hexToBytes(kOldLabelHex), newPath(a.label));

    // ---- 「容器里装结构体」，而且元素内部还有遍历顺序不确定的容器。
    // 撤除那条拒绝之前，对同一个形状请求序列化是 `exit 12` 并在 stderr 里点名成员、元素类型与内层
    // 无序容器（读数已留档）；撤除之后它必须出码成功、往返一致，且两个只有内层插入顺序不同的对象
    // 写出的字节逐字节相同 —— 这一条只有在生成代码能递归进 Inner 自己的生成代码时才成立。
    codegen_containers::Outer oa{};
    codegen_containers::Outer ob{};
    const unsigned innerA[] = { 5u, 1u, 9u };
    const unsigned innerB[] = { 9u, 5u, 1u };
    for (unsigned item = 0; item < 2; ++item)
    {
        oa.items.push_back(codegen_containers::Inner{});
        ob.items.push_back(codegen_containers::Inner{});
        // 两个对象的内容完全相同：同样的键、同样的值，只有**插入顺序**不同。
        for (unsigned i = 0; i < 3; ++i)
        {
            oa.items[item].m.emplace(innerA[i], innerA[i] * 10u);
            ob.items[item].m.emplace(innerB[i], innerB[i] * 10u);
        }
    }
    const castl::vector<uint8_t> oaBytes = writeAny(oa);
    const castl::vector<uint8_t> obBytes = writeAny(ob);
    const bool outerStable = sameBytes(oaBytes, obBytes);

    codegen_containers::Outer oBack{};
    std::size_t outerPosition = 0;
    const bool outerRead =
        cacore::codegen::CodeGenSerializer<codegen_containers::Outer>::deserialize(oBack, oaBytes, outerPosition) &&
        outerPosition == oaBytes.size() &&
        oBack.items.size() == oa.items.size() &&
        oBack.items[0].m == oa.items[0].m &&
        oBack.items[1].m == oa.items[1].m;

    // 对照：内层内容改一个值必须换掉那串字节，否则上面那条「相同」是空的。
    codegen_containers::Outer oControl = oa;
    oControl.items[0].m.emplace(777u, 1u);
    const bool outerControl = !sameBytes(writeAny(oControl), oaBytes);

    std::printf("CodeGen container-of-struct bytes stable = %d, round-trip = %d, control differs = %d (bytes=%zu)\n",
                outerStable ? 1 : 0, outerRead ? 1 : 0, outerControl ? 1 : 0, oaBytes.size());
    std::printf("CodeGen container bytes stable = %d (bytes=%zu)\n", stableBytes ? 1 : 0, ba.size());
    std::printf("CodeGen container control differs = %d\n", controlDiffers ? 1 : 0);
    std::printf("CodeGen container read-back clean = %d\n", readClean ? 1 : 0);
    std::printf("CodeGen ordered members match the old path = %d (vector/map/string)\n", orderedSame ? 1 : 0);
    const bool ok = stableBytes && controlDiffers && readClean && orderedSame &&
                    outerStable && outerRead && outerControl;
    if (ok)
        std::puts("CodeGen container serialization PASSED");
    return ok;
}
