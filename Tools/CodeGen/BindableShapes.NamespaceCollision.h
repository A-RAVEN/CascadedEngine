#pragma once
// 映射后全名碰撞夹具（csharp-bindings-namespace-alignment 1.1，design D1）。
// `AB` 与 `Ab` 在层内分段规则下都映射为 `Ab`，类名同为 `Widget` ⇒ C# 全名都是 `Ab.Widget`，
// 生成器必须非零退出并点名双方（`AB::Widget` / `Ab::Widget`）。
// 这份头**不进** CODEGEN_BINDINGS：它只由拒绝探针按需起 CodeGen.exe。
#include <CodeGenMeta.h>
#include <stdint.h>

namespace AB
{
    CA_BIND()
    class Widget
    {
    public:
        virtual uint32_t GetArea() const = 0;
        virtual ~Widget() = default;
    };
}

namespace Ab
{
    CA_BIND()
    class Widget
    {
    public:
        virtual uint32_t GetArea() const = 0;
        virtual ~Widget() = default;
    };
}
