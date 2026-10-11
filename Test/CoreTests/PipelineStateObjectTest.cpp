//
// PipelineStateObjectTest.cpp — 真实类型（CPipelineStateObject 一族）的迁移判据。
//
// 三件事：
//   ① 字节对拍：生成代码写出来的字节，与**旧运行时**写出来的字节逐字节相同。
//      旧的一侧在这里是写死的期望值 —— 它是 2026-10-03 用旧序列化器（加几行老描述符，
//      描述符只活在那次测量的临时相里）实测出来的 301 个字节，
//      原始读数见本次改动说明。旧头删除之后这条用例仍然能跑，这正是它写死字面量的原因。
//   ② 往返：把同一串字节读回来，内容与写出去的一致。
//   ③ 既有哈希调用点用的是新特化：内容相同、填充字节不同的两个对象，既有入口算出的哈希相同、
//      `HashObj` 判等为真（哈希按字段算，不是按整块字节）。
//
#include <CPipelineStateObject.h>
#include <HashKeys.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "StencilStates.codegen.h"
#include "DepthStencilStates.codegen.h"
#include "RasterizerStates.codegen.h"
#include "SingleColorAttachmentBlendStates.codegen.h"
#include "ColorAttachmentsBlendStates.codegen.h"
#include "CPipelineStateObject.codegen.h"
// 一个类型一份产物里反射 / 序列化 / 哈希三节都在，所以"成员类型的特化"这件事只需要记一行：
// 少 include 一个类型，嵌套成员就会退回到"整块字节"那条路（填充一变哈希就变）。这一点由下面第一条读数钉住。

namespace
{
    // 旧运行时写出的 301 个字节（062 个十六进制字符 = 301 字节 × 2）。
    const char* const kExpectedOldBytesHex =
        "0100030000000000000000000000000000000000000000000000000000000007000000000000000000000000000000000000"
        "000000000000000000000000000000000000000100000000000000000020400800000000000000010f020000000300000001"
        "000000000000000000000000000000010f020000000300000001000000000000000000000000000000010f02000000030000"
        "0001000000000000000000000000000000010f020000000300000001000000000000000000000000000000010f0200000003"
        "00000001000000000000000000000000000000010f020000000300000001000000000000000000000000000000010f020000"
        "000300000001000000000000000000000000000000010f020000000300000001000000000000000000000000000000000000"
        "00";

    castl::vector<uint8_t> hexToBytes(const char* hex)
    {
        castl::vector<uint8_t> out;
        const std::size_t n = std::strlen(hex);
        for (std::size_t i = 0; i + 1 < n; i += 2)
        {
            const auto nibble = [](char c) -> unsigned {
                return (c >= '0' && c <= '9') ? static_cast<unsigned>(c - '0')
                                              : static_cast<unsigned>(c - 'a' + 10);
            };
            out.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
        }
        return out;
    }

    // 探针里那个对象的同一份内容。
    CPipelineStateObject makeObject()
    {
        CPipelineStateObject pso{};
        pso.rasterizationStates.cullMode = ECullMode::eBack;
        pso.rasterizationStates.lineWidth = 2.5f;
        pso.depthStencilStates.depthTestEnable = true;
        pso.depthStencilStates.stencilStateFront.reference = 7u;
        pso.colorAttachments = ColorAttachmentsBlendStates::AlphaTransparent();
        pso.msCount = EMultiSampleCount::e1;
        return pso;
    }
} // namespace

bool RunPipelineStateObjectTest()
{
    // 判据③的前半段是编译期的：哈希入口（`cacore::HashObj` 内部用的那个哈希对象）必须看得到生成的特化。
    // 这一行就是"生成的特化在实际调用点生效"的机器可核形式 —— 生成头没被 include 时它编译不过。
    static_assert(cacore::codegen::has_custom_hash_func<CPipelineStateObject, cacore::codegen::hasher<>>,
                  "哈希入口看不到 CPipelineStateObject 的生成特化：4.3 的『生效』不成立");

    const CPipelineStateObject pso = makeObject();

    const castl::vector<uint8_t> expected = hexToBytes(kExpectedOldBytesHex);
    castl::vector<uint8_t> written;
    cacore::codegen::CodeGenSerializer<CPipelineStateObject>::serialize(pso, written);
    const bool bytesMatchOld =
        written.size() == expected.size() &&
        (written.empty() || std::memcmp(written.data(), expected.data(), written.size()) == 0);

    // 往返：把期望字节读回来，内容必须与写出去的一致。
    CPipelineStateObject readBack{};
    std::size_t position = 0;
    const bool readOk =
        cacore::codegen::CodeGenSerializer<CPipelineStateObject>::deserialize(readBack, expected, position) &&
        position == expected.size();
    const bool contentMatches =
        readBack.rasterizationStates.cullMode == pso.rasterizationStates.cullMode &&
        readBack.rasterizationStates.lineWidth == pso.rasterizationStates.lineWidth &&
        readBack.depthStencilStates.depthTestEnable == pso.depthStencilStates.depthTestEnable &&
        readBack.depthStencilStates.stencilStateFront.reference ==
            pso.depthStencilStates.stencilStateFront.reference &&
        readBack.msCount == pso.msCount &&
        readBack.colorAttachments == pso.colorAttachments;

    // ---- 判据③的后半段（运行时）：同样的内容、不同的填充字节。
    // 先把整块内存写成两组不同的值，再逐字段赋成同一份内容 —— 于是"字段全同、填充不同"。
    CPipelineStateObject paddedA{};
    CPipelineStateObject paddedB{};
    std::memset(&paddedA, 0x00, sizeof(CPipelineStateObject));
    std::memset(&paddedB, 0xAA, sizeof(CPipelineStateObject));
    // 逐成员赋值只写有意义的字节；成员之间与对象末尾的填充保留两组不同的值。
    // （不能整体赋值 —— 整体赋值会把填充一起拷过去，那样"填充不同"这条前提就不成立了。）
    paddedA.rasterizationStates = pso.rasterizationStates;
    paddedA.depthStencilStates = pso.depthStencilStates;
    paddedA.colorAttachments = pso.colorAttachments;
    paddedA.msCount = pso.msCount;
    paddedB.rasterizationStates = pso.rasterizationStates;
    paddedB.depthStencilStates = pso.depthStencilStates;
    paddedB.colorAttachments = pso.colorAttachments;
    paddedB.msCount = pso.msCount;
    // `RasterizerStates` 里两个 `bool` 之后、第一个枚举之前有 2 个填充字节（枚举要 4 字节对齐）：
    // 把这两组填充写成不同的值。字段本身仍然逐字段相等 —— 下面两条读数就是这一点的证据。
    for (std::size_t i = 2; i < 4; ++i)
    {
        reinterpret_cast<unsigned char*>(&paddedA.rasterizationStates)[i] = 0x11;
        reinterpret_cast<unsigned char*>(&paddedB.rasterizationStates)[i] = 0x22;
    }
    const bool fieldsEqual = paddedA.rasterizationStates == paddedB.rasterizationStates &&
                             paddedA.depthStencilStates == paddedB.depthStencilStates &&
                             paddedA.colorAttachments == paddedB.colorAttachments &&
                             paddedA.msCount == paddedB.msCount;
    const bool paddingDiffers = std::memcmp(&paddedA, &paddedB, sizeof(CPipelineStateObject)) != 0;

    cacore::codegen::hasher<> hasherA;
    cacore::codegen::hasher<> hasherB;
    hasherA.hash(paddedA);
    hasherB.hash(paddedB);
    const bool hashIgnoresPadding = hasherA.getHash() == hasherB.getHash();

    const cacore::HashObj<CPipelineStateObject> keyA{ paddedA };
    const cacore::HashObj<CPipelineStateObject> keyB{ paddedB };
    const bool keysEqual = (keyA == keyB);

    // 对照：内容改一个字节，哈希与判等都必须变 —— 否则上面两条是空的。
    CPipelineStateObject changed = pso;
    changed.rasterizationStates.lineWidth = 9.0f;
    cacore::codegen::hasher<> hasherC;
    hasherC.hash(changed);
    const bool controlDiffers = hasherC.getHash() != hasherA.getHash() &&
                                !(cacore::HashObj<CPipelineStateObject>{ changed } == keyA);

    std::printf("CPipelineStateObject bytes == old runtime = %d (bytes=%zu), read-back = %d, content = %d\n",
                bytesMatchOld ? 1 : 0, written.size(), readOk ? 1 : 0, contentMatches ? 1 : 0);
    std::printf("CPipelineStateObject hash ignores padding = %d (fields equal = %d, overall bytes differ = %d, "
                "HashObj equal = %d, control differs = %d)\n",
                hashIgnoresPadding ? 1 : 0, fieldsEqual ? 1 : 0, paddingDiffers ? 1 : 0, keysEqual ? 1 : 0,
                controlDiffers ? 1 : 0);

    const bool ok = bytesMatchOld && readOk && contentMatches && fieldsEqual && paddingDiffers &&
                    hashIgnoresPadding && keysEqual && controlDiffers;
    if (ok)
        std::puts("CPipelineStateObject migration PASSED");
    return ok;
}
