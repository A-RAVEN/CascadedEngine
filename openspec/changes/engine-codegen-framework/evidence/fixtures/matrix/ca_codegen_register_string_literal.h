#pragma once
#include "CodeGenMeta.h"

namespace probe {
    struct Target { int a; float b; };
static const char* kText = "CA_CODEGEN_REGISTER("probe::Target")";
}
