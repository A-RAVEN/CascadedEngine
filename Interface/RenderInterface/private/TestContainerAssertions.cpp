// codegen-container-semantics task 2.3: the container fixture's generated metadata is CONSUMED here —
// inside a real engine target (`Rendering`), not merely byte-compared against a committed copy. The
// main spec requires exactly this: "generated code SHALL be #included by at least one compile unit of a
// real engine target, with a compile-time assertion on the generated table".
#include "TestContainer.h"        // codegen_containers::Containers (private/, same target)
#include "Containers.Reflection.h" // generator output (generated/, on this target's include path)
#include <CodeGenMeta.h>          // cacore::codegen::TypeDescriptor + FieldFlags
#include <Reflection.h>           // careflection::containerStates — the runtime's OWN container rule

namespace
{
    using T = codegen_containers::Containers;
    using cacore::codegen::CodeGenFieldInfo;
    using cacore::codegen::FieldFlags;
    using cacore::codegen::has_flag;
    using cacore::codegen::TypeDescriptor;
    using cacore::codegen::TypeKind;

    constexpr const CodeGenFieldInfo* field(const char* name)
    {
        for (std::size_t i = 0; i < TypeDescriptor<T>::fieldCount; ++i)
            if (TypeDescriptor<T>::fields()[i].name == name) // same literal in the generated table
                return &TypeDescriptor<T>::fields()[i];
        return nullptr;
    }

    // Ordered containers: Container flag only.
    static_assert(has_flag(field("vec")->flags, FieldFlags::Container), "'vec' must be a container");
    static_assert(!has_flag(field("vec")->flags, FieldFlags::Unordered), "'vec' is ordered");
    static_assert(field("vec")->element != nullptr, "'vec' must carry an element description");
    static_assert(field("vec")->element->kind == TypeKind::UInt,
                  "'vec' element must be UInt (canonicalized from `unsigned`)");

    // Unordered map: Container + Unordered, with BOTH key and element descriptions.
    static_assert(has_flag(field("umap")->flags, FieldFlags::Container), "'umap' must be a container");
    static_assert(has_flag(field("umap")->flags, FieldFlags::Unordered), "'umap' must be marked unordered");
    static_assert(field("umap")->key != nullptr && field("umap")->element != nullptr,
                  "'umap' must carry key and element descriptions");
    static_assert(field("umap")->key->kind == TypeKind::UInt, "'umap' key must be UInt");
    static_assert(field("umap")->element->kind == TypeKind::UInt, "'umap' element must be UInt");

    // Ordered containers must NOT be marked unordered (the whole point of the canonical write order).
    static_assert(!has_flag(field("omap")->flags, FieldFlags::Unordered), "'omap' is ordered");
    static_assert(!has_flag(field("oset")->flags, FieldFlags::Unordered), "'oset' is ordered");
    static_assert(has_flag(field("uset")->flags, FieldFlags::Unordered), "'uset' must be marked unordered");

    // A container of containers is asserted again once task 3.2 adds that fixture member back (the old
    // runtime cannot read such a member back — see the note in TestContainer.h).

    // Opaque elements are flagged; the plain member has no container facts at all (control).
    static_assert(!has_flag(field("plain")->flags, FieldFlags::Container), "'plain' is not a container");
    // Task 2.2: a C array is a container by extent and its element description is ONE dimension down
    // (`int[2][3]` -> `int[3]` -> `int`), so the disk shape (extent, then per dimension) is expressed.
    static_assert(has_flag(field("grid")->flags, FieldFlags::Container), "'grid' must be a container");
    static_assert(field("grid")->element != nullptr, "'grid' must carry an element description");
    static_assert(field("grid")->element->kind == TypeKind::Array,
                  "'grid' element must be the array type `int[3]` (TypeKind::Array)");
    static_assert(field("grid")->element->element != nullptr &&
                  field("grid")->element->element->kind == TypeKind::Int,
                  "`int[3]`'s own element must be int");
    static_assert(field("plain")->key == nullptr && field("plain")->element == nullptr,
                  "'plain' must not carry element descriptions");

    // Task 2.5: parity with the runtime's own rule. The tool decides "is this member a container" and
    // the old runtime decides it separately (`containerStates<T>::is_container_with_size`); if the two
    // ever drift, one side would emit container code for something the other treats as a plain struct.
    // `plain` is the control: both sides must say "not a container" for it.
    template<typename T>
    constexpr bool parity(const char* name)
    {
        return has_flag(field(name)->flags, FieldFlags::Container) ==
               careflection::containerStates<T>::is_container_with_size;
    }

    static_assert(parity<std::vector<unsigned>>("vec"), "tool and runtime disagree about 'vec'");
    static_assert(parity<std::unordered_map<unsigned, unsigned>>("umap"),
                  "tool and runtime disagree about 'umap'");
    static_assert(parity<std::map<unsigned, unsigned>>("omap"), "tool and runtime disagree about 'omap'");
    static_assert(parity<std::set<unsigned>>("oset"), "tool and runtime disagree about 'oset'");
    static_assert(parity<std::unordered_set<unsigned>>("uset"),
                  "tool and runtime disagree about 'uset'");
    static_assert(parity<unsigned>("plain"), "tool and runtime disagree about 'plain' (control)");
}
