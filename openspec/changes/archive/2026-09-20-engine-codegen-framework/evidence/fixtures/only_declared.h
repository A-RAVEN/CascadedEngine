#pragma once
#include "CodeGenMeta.h"
// Red case found by the round-32 adversarial review (task 9.55): the file annotates a type and then only
// DECLARES it. There is no definition anywhere, so the tool cannot know the member list.
//
// Measured on the 9.54-only binary (source blob 209e23b): exit 0 and an artifact whose body asserts
//   static constexpr std::size_t fieldCount = 0;
//   static constexpr const FieldDescriptor* fields() { return nullptr; }
// for a type it never saw defined -- the same "empty table, exit 0" shape task 9.50(1) was opened for,
// and the generated file's own preamble says the reflected type must be fully defined.
//
// On the fixed binary the marker binds to no definition, so this file has no annotated type at all and
// the tool reports exactly that and writes nothing. The registered variant that DOES reach the cursor
// search is in only_declared_registered.h.
namespace audit_ns
{
    CA_REFLECT(serializable)
    struct OnlyDeclared;
}
