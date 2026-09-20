#pragma once
#include "CodeGenMeta.h"
#define CA_REFLECTION(Type, ...) static_assert(true, "CA_REFLECTION:" #Type)
namespace probe {
    struct Target { int a; float b; };
static const char* kRaw = R"x(CA_REFLECTION(Target, a, b))x";
}
