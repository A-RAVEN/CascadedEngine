#pragma once
#include "CodeGenMeta.h"

namespace probe {
    struct Target { int a; float b; };
// note \
CA_CODEGEN_REGISTER("probe::Target")
}
