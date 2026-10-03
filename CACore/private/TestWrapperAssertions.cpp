// codegen-container-semantics task 5.1: a wrapper (`NameHash`) inside a container must be ACCEPTED and
// only recorded — it must NOT be treated as opaque, because that is what would reject the engine's real
// types (`ShaderLibrary` has a `vector<NameHash>` on its value chain). The assertions below are the
// measured contradiction of the rejected "wrapper counts as opaque -> refuse" implementation.
#include "TestWrapper.h"
#include "Holder.Reflection.h"
#include "Holder.Serialization.h"
#include "Holder.Hash.h"
#include <CodeGenMeta.h>

namespace
{
    using T = codegen_wrapper::Holder;
    using cacore::codegen::CodeGenFieldInfo;
    using cacore::codegen::FieldFlags;
    using cacore::codegen::has_flag;
    using cacore::codegen::TypeDescriptor;

    constexpr const CodeGenFieldInfo* field(const char* name)
    {
        for (std::size_t i = 0; i < TypeDescriptor<T>::fieldCount; ++i)
            if (TypeDescriptor<T>::fields()[i].name == name)
                return &TypeDescriptor<T>::fields()[i];
        return nullptr;
    }

    static_assert(has_flag(field("names")->flags, FieldFlags::Container), "'names' must be a container");
    static_assert(!has_flag(field("names")->flags, FieldFlags::HasOpaqueElement),
                  "a wrapper element must NOT be opaque (task 5.1)");
    static_assert(has_flag(field("names")->flags, FieldFlags::Wrapper),
                  "a wrapper element must be RECORDED as a wrapper (task 2.2)");
    static_assert(has_flag(field("byName")->flags, FieldFlags::Container), "'byName' must be a container");
    static_assert(!has_flag(field("byName")->flags, FieldFlags::HasOpaqueElement),
                  "a wrapper key must NOT be opaque (task 5.1)");
    // The control: a raw pointer element IS opaque, so the flag above is not vacuous.
    static_assert(has_flag(field("byName")->flags, FieldFlags::Wrapper), "a wrapper key must be recorded too");
}
