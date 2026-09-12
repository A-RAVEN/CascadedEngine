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
} // namespace codegen_private_test
