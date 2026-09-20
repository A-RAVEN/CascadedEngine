#pragma once
#include "CodeGenMeta.h"
namespace audit_ns
{
    CA_REFLECT(nonserializable)               // 9.42 red case: substring used to pass
    struct NoSer2 { int a; float b; };
}
