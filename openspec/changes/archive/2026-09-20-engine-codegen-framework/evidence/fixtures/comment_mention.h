#pragma once
#include "CodeGenMeta.h"
namespace audit_ns
{
    CA_REFLECT(ui /* serializable */)          // 9.42 red case: mention inside a comment
    struct NoSer4 { int a; float b; };
}
