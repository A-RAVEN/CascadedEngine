#pragma once
#include "CodeGenMeta.h"

namespace probe {
    struct Target { int a; float b; };
#define REG(T) CA_REFLECT(serializable)
}
