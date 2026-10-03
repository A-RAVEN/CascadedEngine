#pragma once
// codegen-container-semantics task 2.3: the container fixture. It lives inside an engine module
// (`Interface/RenderInterface`) on purpose — the owner directory decides the compile environment used
// to parse it, and the module-root environment is the one that can resolve the engine's own headers.
//
// The include set is deliberately small: `CASTL/CAContainerBase.h` (the `castl` namespace) plus the
// STL containers under test. Pulling in `CASTL/CAUnorderedMap.h` would drag `Hasher.h` → `CAHash.h` →
// `<sha256.h>`, which is NOT on every module's include path (measured: exit 6 under `Tools/CodeGen`).
#include "CodeGenMeta.h"
#include <CASTL/CAContainerBase.h>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace codegen_containers
{
    // Task 3.3: an element with its own generated hash. `h.hash(element)` takes the trivially copyable
    // shortcut (raw bytes, so -0.0f and +0.0f differ); the element's own trait normalizes the float.
    CA_REFLECT(serializable)
    struct HasFloat
    {
        float f;
        friend bool operator==(const HasFloat& a, const HasFloat& b) { return a.f == b.f; }
    };

    CA_REFLECT(serializable)
    struct Containers
    {
        std::vector<unsigned>                  vec;      // ordered, value elements
        std::unordered_map<unsigned, unsigned> umap;     // unordered, key + value
        std::map<unsigned, unsigned>           omap;     // ordered map
        std::set<unsigned>                     oset;     // ordered set
        std::unordered_set<unsigned>           uset;     // unordered set
        std::vector<std::vector<int>>          nested;   // ordered container of containers (task 3.2)
        std::vector<std::unordered_map<unsigned, unsigned>> vecOfUmap;   // nested unordered (task 3.2)
        std::unordered_map<unsigned, std::unordered_map<unsigned, unsigned>> umapOfUmap;  // task 3.2
        std::unordered_map<unsigned, unsigned> umaps[2];                 // C array of containers (task 3.2)
        std::unordered_map<unsigned, float>    floatMap;                 // float normalization (task 3.3)
        std::vector<HasFloat>                  floats;                  // element trait (task 3.3)
        signed int                             grid[2][3]; // C array: element description is `int[3]`
        std::string                            label;    // ordered container (task 3.5)
        unsigned                               plain;    // not a container (control)
    };
}
