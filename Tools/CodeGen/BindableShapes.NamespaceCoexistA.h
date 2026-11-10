#pragma once
// 跨命名空间同短名共存夹具 A（csharp-bindings-namespace-alignment 1.1/1.3，design D1/D6）。
// 与同名 B 头配对：`ns1::Device` / `ns2::Device` 同短名、`a_b::Widget` / `a::b::Widget` 同短名，
// 旧判据（build.py 的短名冲突）在两份头的清单聚合处非零拒绝；改造后必须各自成面、构建全绿。
#include <CodeGenMeta.h>
#include <stdint.h>

namespace ns1
{
    CA_BIND()
    class Device
    {
    public:
        virtual uint32_t GetId() const = 0;
        virtual void SetId(uint32_t value) = 0;
        virtual ~Device() = default;
    };
}

namespace a_b
{
    CA_BIND()
    class Widget
    {
    public:
        virtual uint32_t GetArea() const = 0;
        virtual ~Widget() = default;
    };
}
