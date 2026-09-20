#pragma once
#include "CodeGenMeta.h"

namespace probe {
    struct Target { int a; float b; };
static const char* kRaw = R"x(CA_CODEGEN_REGISTER("probe::Target"))x";
}
