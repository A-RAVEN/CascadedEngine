#pragma once
// 跨命名空间同短名共存夹具 B（csharp-bindings-namespace-alignment 1.1/1.3，design D1/D6）。
// 与 A 头配对：`ns2::Device` / `a::b::Widget`，短名分别与 A 头的 Device / Widget 相同。
#include <CodeGenMeta.h>
#include <stdint.h>

namespace ns2
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

namespace a
{
    namespace b
    {
        CA_BIND()
        class Widget
        {
        public:
            virtual uint32_t GetArea() const = 0;
            virtual ~Widget() = default;
        };
    }
}
