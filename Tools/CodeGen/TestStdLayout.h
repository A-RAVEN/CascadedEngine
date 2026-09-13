// TestStdLayout.h — fixture for task 9.4: the ONLY fixture whose layout depends on the debug-STL
// macros, which is exactly the mechanism behind the /MDd offset mismatch.
//
// Mechanism: /MDd makes cl.exe define _DEBUG and _ITERATOR_DEBUG_LEVEL=2 implicitly. Debug STL adds a
// _Container_proxy* to std::string, so sizeof(std::string) grows (32 -> 40 on MSVC x64), which shifts
// every member after it. The build.py env translator used to strip /MDd and pass neither macro, so the
// sidecar laid the type out with the /MD (release) rules.
//
// The type is a plain public struct so the extraction path can serve it (task 9.1 guard does not apply).
#pragma once

#include "CodeGenMeta.h"

#include <string>

namespace codegen_stdlayout_test
{
    CA_REFLECT(serializable)
    struct StdLayout
    {
        int         head;
        std::string key;   // sizeof differs between /MD and /MDd
        int         tail;
    };
}
