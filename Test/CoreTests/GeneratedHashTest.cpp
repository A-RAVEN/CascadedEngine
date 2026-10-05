//
// GeneratedHashTest.cpp — the CodeGen hash/equality emitter.
//
// Proves the emitted `cacore::codegen::custom_hash_trait<T>` (the hook the CodeGen hasher calls via
// `has_custom_hash_func`) hashes each field in declaration order (NOT raw whole-struct bytes,
// => padding/layout independent) and normalizes floats: hash(-0.0)==hash(+0.0), NaN hashes stably, and
// `equal` treats -0.0/+0.0 and NaN/NaN as equal.
//
#include <CodeGenHash.h>
#include <HashKeys.h>   // 三个哈希键类型（NameHash / PathHash / HashObj）在新头里
#include <ShaderResourceHandle.h>   // 两个资源句柄（哈希键）与它们的手写哈希特化
#include <cstdio>
#include <cstring>
#include <limits>
#include "TestEmit.h"
#include "EmitSample.Hash.h"

bool RunGeneratedHashTest()
{
    using EmitSample = codegen_demo::EmitSample;
    using Trait = cacore::codegen::custom_hash_trait<EmitSample>;

    // 判据之一：生成的特化必须由**新运行时**的哈希对象驱动 —— 把 CodeGenHash.h 里的
    // 哈希对象拿掉，这一行编译不过。
    static_assert(cacore::codegen::has_custom_hash_func<EmitSample, cacore::codegen::hasher<>>,
                  "生成的特化对 CodeGen 自己的哈希对象不可见：新哈希运行时没有接上");

    auto hashOf = [](EmitSample const& o)
    {
        cacore::codegen::hasher<> hasher;
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

    // ---- **入口**（`cacore::codegen::hasher`）必须走生成的特化，而不是"整块字节"那条捷径。
    // 这就是分派顺序的判据：如果哈希对象把"平凡可拷贝"排在自定义特化前面，这一行算出来的就是
    // 整块字节（含填充）的哈希，与按字段算的值不等 —— 判据在那个状态下为红。
    auto legacyEntryHash = [](EmitSample const& o)
    {
        cacore::codegen::hasher<> hasher;
        hasher.hash(o);
        return hasher.getHash();
    };
    const bool legacyMatchesFieldwise = legacyEntryHash(a) == hashOf(a);

    // ---- 两个字段之间的填充字节改成不同值，哈希不变（防止退回整块字节）。
    // `EmitSample` 的布局是 a@0（4 字节）、b@4（4）、c@8（1）、填充@9..15、d@16（8），共 24 字节。
    EmitSample padA{ 1, 2.0f, true, 3.0 };
    EmitSample padB = padA;
    for (std::size_t i = 9; i < 16; ++i)
    {
        reinterpret_cast<unsigned char*>(&padA)[i] = 0xAA;
        reinterpret_cast<unsigned char*>(&padB)[i] = 0x55;
    }
    const bool paddingDiffers = std::memcmp(&padA, &padB, sizeof(EmitSample)) != 0;
    const bool fieldsEqual = padA.a == padB.a && padA.b == padB.b && padA.c == padB.c && padA.d == padB.d;
    const bool hashIgnoresPadding = legacyEntryHash(padA) == legacyEntryHash(padB);
    // 对照：同两个对象走"整块字节"那条路会得到不同的哈希 —— 证明上面那条不是因为填充没写进去。
    cacore::codegen::hasher<> rawA;
    cacore::codegen::hasher<> rawB;
    rawA.hash_raw(&padA, sizeof(EmitSample));
    rawB.hash_raw(&padB, sizeof(EmitSample));
    const bool rawBytesDiffer = rawA.getHash() != rawB.getHash();

    std::printf("CodeGen legacy hash entry uses the generated trait = %d (legacy=%llu fieldwise=%llu)\n",
                legacyMatchesFieldwise ? 1 : 0, (unsigned long long)legacyEntryHash(a),
                (unsigned long long)hashOf(a));
    std::printf("CodeGen hash ignores padding = %d (padding differs = %d, fields equal = %d, raw bytes differ = %d)\n",
                hashIgnoresPadding ? 1 : 0, paddingDiffers ? 1 : 0, fieldsEqual ? 1 : 0, rawBytesDiffer ? 1 : 0);

    // ---- 构建内 5 个既有哈希特化里，能在本编译单元取到值的这 3 个各给一个读数。
    // 改动前后走的是同一条分支（各宿主文件里的 `dispatch_branch_unchanged` 断言钉住这一点），
    // 所以这两个读数必须逐字相同。另外两个宿主（`ComPtr<ID3Blob>` / `VulkanShaderResourceSet`）
    // 在自己的模块里，读数就是那两行编译期断言本身。
    auto printLegacyHostHash = [](const char* name, auto const& host)
    {
        cacore::codegen::hasher<> hasher;
        hasher.hash(host);
        std::printf("CodeGen legacy hash host %-12s = %llu\n", name,
                    (unsigned long long)hasher.getHash());
    };
    printLegacyHostHash("NameHash", cacore::NameHash("codegen-probe"));
    printLegacyHostHash("PathHash", cacore::PathHash("dir/codegen-probe.txt"));
    printLegacyHostHash("HashObj<int>", cacore::HashObj<int>(7));
    if (!legacyMatchesFieldwise || !hashIgnoresPadding || !paddingDiffers || !fieldsEqual || !rawBytesDiffer)
    {
        std::puts("Generated hash dispatch FAILED (dispatch order / padding)");
        return false;
    }

    // ---- 两个资源句柄（`ImageHandle` / `BufferHandle`）是**哈希键**，不是落盘载荷：它们各有一个
    // `castl::shared_ptr` 成员，指针成员是工具明令拒绝的形状（实测探针：给两个类补上 `CA_REFLECT` 与
    // `CA_CODEGEN_FRIEND` 之后，`--emit-all` 对 ImageHandle 报 `exit 15 … member 'm_ExternalManagedTexture'
    // is a managed pointer ('castl::shared_ptr<GPUTexture>') …`、对 BufferHandle 报同一形状的 15 并点名
    // `m_ExternalManagedBuffer`）。所以它们按设计给的第二条路在头里手写了 `custom_hash_trait`。
    // 这里钉住那条规则的语义：键相同 → 哈希相同（指针地址不进哈希）、键不同 → 哈希不同。
    {
        // 编译期：既有入口必须看得到那两份手写特化（少一份这一行就编不过）。
        static_assert(cacore::codegen::has_custom_hash_func<graphics_backend::BufferHandle,
                                                            cacore::codegen::hasher<>>,
                      "哈希入口看不到 BufferHandle 的手写特化");
        static_assert(cacore::codegen::has_custom_hash_func<graphics_backend::ImageHandle,
                                                            cacore::codegen::hasher<>>,
                      "哈希入口看不到 ImageHandle 的手写特化");

        // 句柄都用**不经过 `Unique()`** 的构造路径：`Unique` 会碰 `ThreadID::Get()` /
        // `ThreadLocalID::Get()`（实现在 Rendering 模块里，CoreTests 不链接那个库）。
        // 键相同（都是 `Default()`）+ 类型不同 → 哈希必须不同；两个默认句柄 → 哈希相同。
        const graphics_backend::BufferHandle bufferA;
        const graphics_backend::BufferHandle bufferB;
        const graphics_backend::BufferHandle bufferExternal(castl::shared_ptr<graphics_backend::GPUBuffer>{});
        const graphics_backend::ImageHandle imageA;
        const graphics_backend::ImageHandle imageB;
        const graphics_backend::ImageHandle imageExternal(castl::shared_ptr<graphics_backend::GPUTexture>{});

        auto hashOfHandle = [](auto const& handle)
        {
            cacore::codegen::hasher<> hasher;
            hasher.hash(handle);
            return static_cast<unsigned long long>(hasher.getHash());
        };
        const auto bufferHashA = hashOfHandle(bufferA);
        const auto imageHashA = hashOfHandle(imageA);
        const bool handleSameKeySameHash = bufferHashA == hashOfHandle(bufferB) && imageHashA == hashOfHandle(imageB);
        const bool handleTypeDiffers = bufferHashA != hashOfHandle(bufferExternal) &&
                                       imageHashA != hashOfHandle(imageExternal);
        const bool handleKindsDiffer = bufferHashA != imageHashA;

        std::printf("CodeGen resource handles hash keys = %d (同键同位 → 相同 = %d, 同键不同类型 → 不同 = %d, "
                    "两种句柄不同 = %d)\n",
                    (handleSameKeySameHash && handleTypeDiffers && handleKindsDiffer) ? 1 : 0,
                    handleSameKeySameHash ? 1 : 0, handleTypeDiffers ? 1 : 0, handleKindsDiffer ? 1 : 0);
        if (!handleSameKeySameHash || !handleTypeDiffers || !handleKindsDiffer)
        {
            std::puts("Generated hash of resource handles FAILED");
            return false;
        }
    }

    std::puts("CodeGen hash/equality test PASSED");
    return true;
}

