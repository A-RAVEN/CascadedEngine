#pragma once
#include "CodeGenMeta.h"

namespace probe {
    struct Target { int a; float b; };
#ifdef NEVER_DEFINED_ANYWHERE
CA_CODEGEN_REGISTER("probe::Target")
#endif
}
