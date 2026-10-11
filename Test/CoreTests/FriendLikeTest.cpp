// 私有成员 + 类内友元入口（`CA_CODEGEN_FRIEND`）的消费单元证明。
//
// 工具那边只保证"出码成功"（退出码 0）；这里才证明**生成的代码真的能编译** —— 生成物按成员名读私有
// 成员（`cgWriteValue(w, obj.m_a)`、`cgEqualValue(a.m_a, b.m_a)`、`offsetof(T, m_a)`），没有那四条
// friend 声明就会在**生成头里**报 C2248。所以这个文件编得过，就是"友元入口有效"的读数。
//
// 注意本文件自己**不能**碰那些私有成员：友元只给了生成的四个入口，没给测试。要构造内容不同的两个
// 对象，只能走 `FriendLike::Make` / `WithDifferentA` 这两个公开接口。
//
// 对照组：`Tools/CodeGen/TestPrivate.h` 里的 `PrivateLike` 是同一个形状但没有那一行，工具对它仍然
// 以退出码 5 拒绝（实测：`CodeGen.exe Tools/CodeGen/TestPrivate.h PrivateLike -I CACore/header
// --emit-reflection` → exit 5）。
#include "TestPrivate.h"

#include "FriendLike.codegen.h"

#include <CodeGenMeta.h>
#include <CodeGenSerialization.h>
#include <cstdio>
#include <cstring>

namespace
{
    using T = codegen_private_test::FriendLike;
    using cacore::codegen::TypeDescriptor;

    // 字段表：4 个成员，偏移与写死的期望值比（`pub` 0、`m_a` 4、`m_b` 8、`m_c` 16 —— 结构体没有基类，
    // double 对齐到 8）。这几个数是照着"编译器怎么排"写下来的，不是从生成表里抄的。
    static_assert(TypeDescriptor<T>::fieldCount == 4, "FriendLike 的字段表条数不是 4");
    static_assert(TypeDescriptor<T>::fields()[0].offset == 0, "第 0 条不是 pub(0)");
    static_assert(TypeDescriptor<T>::fields()[1].offset == 4, "第 1 条不是 m_a(4)");
    static_assert(TypeDescriptor<T>::fields()[2].offset == 8, "第 2 条不是 m_b(8)");
    static_assert(TypeDescriptor<T>::fields()[3].offset == 16, "第 3 条不是 m_c(16)");

    bool sameBytes(const castl::vector<unsigned char>& a, const castl::vector<unsigned char>& b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }
} // namespace

bool RunFriendLikeTest()
{
    const T in = T::Make(11, 2.5f, 6.25);

    // 生成的序列化特化读的是私有成员 —— 这一段能编过就证明友元入口真的开了访问权。
    castl::vector<unsigned char> bytes;
    cacore::codegen::CodeGenSerializer<T>::serialize(in, bytes);

    T out{};
    std::size_t position = 0;
    const bool readBack = cacore::codegen::CodeGenSerializer<T>::deserialize(out, bytes, position) &&
                          position == bytes.size() && out.Pub() == in.Pub() && out.A() == in.A() &&
                          out.B() == in.B() && out.C() == in.C();

    // 反向对照：只把 m_a 改大 1，字节必须跟着变（否则上面那条"读回来一样"是空的）。
    castl::vector<unsigned char> controlBytes;
    cacore::codegen::CodeGenSerializer<T>::serialize(in.WithDifferentA(), controlBytes);
    const bool controlDiffers = !sameBytes(bytes, controlBytes);

    // 相等与哈希同样走生成的特化（一样要访问私有成员）。
    const bool equalOk = cacore::codegen::CodeGenEqual<T>::equal(in, in) &&
                         !cacore::codegen::CodeGenEqual<T>::equal(in, in.WithDifferentA());
    cacore::codegen::hasher<> hasherA, hasherB;
    cacore::codegen::custom_hash_trait<T>::hash(in, hasherA);
    cacore::codegen::custom_hash_trait<T>::hash(in, hasherB);
    const bool hashSame = hasherA.getHash() == hasherB.getHash();

    std::printf("FriendLike 私有成员：往返一致 = %d，字节 = %zu，反向对照字节不同 = %d，相等 = %d，哈希相同 = %d\n",
                readBack ? 1 : 0, bytes.size(), controlDiffers ? 1 : 0, equalOk ? 1 : 0, hashSame ? 1 : 0);

    const bool ok = readBack && controlDiffers && equalOk && hashSame;
    if (ok)
        std::puts("CodeGen private-member friend entry PASSED");
    return ok;
}
