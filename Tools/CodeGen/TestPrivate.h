//
// TestPrivate.h — reproduces the offset=0 bug on real interop handles (task 2.2). Those classes
// (ImageHandle/BufferHandle) keep their data members `private:` and add the friend-access
// CA_PRIVATE_REFLECTION macro. We mirror that shape here to isolate whether the tool's
// clang_Cursor_getOffsetOfField returns 0 for (a) private members and/or (b) a base class.
//
#pragma once

#include "CodeGenMeta.h"

namespace codegen_private_test
{
    struct Base
    {
        int   base_pad[3]; // 12 bytes before any derived member
    };

    CA_REFLECT(serializable)
    struct PrivateLike : Base
    {
    public:
        int pub;   // at offset 12 (after Base)
    private:
        int    m_a;  // at offset 16
        float  m_b;  // at offset 20
        double m_c;  // at offset 24 (aligned to 8)
    };

    // 与上面同一个形状，只在类体里多一行 `CA_CODEGEN_FRIEND` —— 这是老系统 `CA_PRIVATE_REFLECTION`
    // 的等价物。判据是"加这一行就出码成功、不加仍然退出码 5"，两个方向都要有读数。
    //
    // 公开的工厂与取值接口是给消费单元用的：消费单元**不能**直接碰这些私有成员（友元只给了生成的四个
    // 入口），所以要有这条路才能构造出两个内容不同的对象来对拍。
    CA_REFLECT(serializable)
    struct FriendLike
    {
        CA_CODEGEN_FRIEND(FriendLike);
    public:
        int pub = 0;

        FriendLike() = default;

        static FriendLike Make(int a, float b, double c)
        {
            FriendLike r;
            r.m_a = a;
            r.m_b = b;
            r.m_c = c;
            return r;
        }

        // 只把 m_a 改大 1：用来做"内容不同 => 字节必须不同"的反向对照。
        FriendLike WithDifferentA() const
        {
            FriendLike r = *this;
            r.m_a = m_a + 1;
            return r;
        }

        int    Pub() const { return pub; }
        int    A() const { return m_a; }
        float  B() const { return m_b; }
        double C() const { return m_c; }

    private:
        int    m_a = 0;
        float  m_b = 0.0f;
        double m_c = 0.0;
    };
} // namespace codegen_private_test
