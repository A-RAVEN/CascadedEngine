#pragma once
#include "CodeGenMeta.h"
namespace audit_ns
{
    CA_REFLECT(ui)   // deliberately WITHOUT `serializable`
    struct NoSer { int a; float b; };
}
