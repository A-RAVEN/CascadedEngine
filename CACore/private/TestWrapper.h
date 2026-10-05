#pragma once
// The wrapper fixture. It lives in `CACore` on purpose — that
// module's compile units really do carry the hash-library include directory (`Hasher.h` pulls
// `<sha256.h>`), which `Interface/RenderInterface` does not (measured: exit 6 there).
#include "CodeGenMeta.h"
#include "Hasher.h"
#include <unordered_map>
#include <vector>

namespace codegen_wrapper
{
    CA_REFLECT(serializable)
    struct Holder
    {
        std::vector<cacore::NameHash>                  names;   // wrapper element
        std::unordered_map<cacore::NameHash, unsigned> byName;  // wrapper key
        unsigned                               plain;
    };
}
