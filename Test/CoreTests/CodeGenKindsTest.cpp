// Task 1.1 of codegen-container-semantics: the field classifier must canonicalize the type before
// choosing a TypeKind, otherwise every typedef spelling falls into TypeKind::Opaque.
//
// Pinned to Tools/CodeGen/TestKinds.h, whose four members spell the same two types two ways. Measured
// before the fix: the two keyword spellings were TypeKind::UInt while `uint32_t` / `uint64_t` were
// TypeKind::Opaque. These assertions are what turned that into a build failure.
#include "TestKinds.h"          // codegen_kinds::Kinds (Tools/CodeGen on this target's include path)
#include "Kinds.Reflection.h"   // generator output (Tools/CodeGen/generated)

namespace
{
    using K = codegen_kinds::Kinds;
    using cacore::codegen::TypeDescriptor;
    using cacore::codegen::TypeKind;

    static_assert(TypeDescriptor<K>::fieldCount == 4,
                  "the classifier fixture must expose exactly four members");

    static_assert(TypeDescriptor<K>::fields()[0].kind == TypeKind::UInt,
                  "'unsigned int' must classify as TypeKind::UInt");
    static_assert(TypeDescriptor<K>::fields()[1].kind == TypeKind::UInt,
                  "'uint32_t' must classify as TypeKind::UInt — a typedef must not land in Opaque");
    static_assert(TypeDescriptor<K>::fields()[2].kind == TypeKind::UInt,
                  "'unsigned long long' must classify as TypeKind::UInt");
    static_assert(TypeDescriptor<K>::fields()[3].kind == TypeKind::UInt,
                  "'uint64_t' must classify as TypeKind::UInt — a typedef must not land in Opaque");
}
