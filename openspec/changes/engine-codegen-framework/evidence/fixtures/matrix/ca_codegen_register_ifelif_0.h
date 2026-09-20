#pragma once
#include "CodeGenMeta.h"

namespace probe {
    struct Target { int a; float b; };
#if SOME_UNDEFINED_CONDITION
int x;
#elif 0
CA_CODEGEN_REGISTER("probe::Target")
#endif
}
