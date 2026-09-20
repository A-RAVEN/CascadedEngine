#pragma once
#include "CodeGenMeta.h"

namespace probe {
    struct Target { int a; float b; };
#if false
CA_CODEGEN_REGISTER("probe::Target")
#endif
}
