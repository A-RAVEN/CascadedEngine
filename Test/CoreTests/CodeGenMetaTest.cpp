//
// CodeGenMetaTest.cpp — compile-time verification of the CodeGen metadata model + ABI guard.
//
// This is deliberately a static_assert-only test (no int main — CoreTests has its main in
// SerializerTest.cpp). It forces the compiler to instantiate TypeDescriptor<CodeGenProbe> and
// the ABI guard, so ANY drift between the generated metadata and the source-of-truth
// CA_REFLECTION becomes a HARD build error (the task-1.2 oracle), not a runtime surprise.
//
// It also exercises the CA_CODEGEN_REGISTER discovery macro (task 2.1) at namespace scope.

#include <CodeGenMeta.h>
#include <cstddef>

using cacore::codegen::FieldFlags;
using cacore::codegen::TypeDescriptor;
using cacore::codegen::TypeKind;

namespace
{
    // CA_REFLECT marker: attribute-style (options, no type name). This TU includes CodeGenMeta.h
    // and is compiled by CoreTests, proving the marker compiles as a no-op under MSVC (task 6b.1).
    CA_REFLECT(serializable, ui)
    // A probe aggregate, reflected by BOTH the existing macro system (for ABI comparison) and
    // a hand-written TypeDescriptor specialization that simulates the emitter output.
    struct CodeGenProbe
    {
        int   a;
        float b;
        bool  c;
    };
} // namespace

CA_REFLECTION(CodeGenProbe, a, b, c);
CA_CODEGEN_REGISTER("CodeGenProbe");

namespace cacore::codegen
{
    template<>
    struct TypeDescriptor<CodeGenProbe>
    {
        static constexpr const char*              name       = "CodeGenProbe";
        static constexpr std::size_t              fieldCount = 3;

        static constexpr const CodeGenFieldInfo* fields() { return arr; }
        static constexpr CodeGenFieldInfo arr[3] = {
            { "a", "int",   offsetof(CodeGenProbe, a), TypeKind::Int,   FieldFlags::Serializable | FieldFlags::HashInclude },
            { "b", "float", offsetof(CodeGenProbe, b), TypeKind::Float, FieldFlags::Serializable | FieldFlags::HashInclude },
            { "c", "bool",  offsetof(CodeGenProbe, c), TypeKind::Bool,  FieldFlags::HashInclude },
        };
    };
} // namespace cacore::codegen

// -----------------------------------------------------------------------------
// Task 1.1 — metadata model is linear data, fields read back by name/offset/kind/flags.
// -----------------------------------------------------------------------------
static_assert(cacore::codegen::TypeDescriptor<CodeGenProbe>::fieldCount == 3,
    "model: fieldCount must be 3");
static_assert(cacore::codegen::TypeDescriptor<CodeGenProbe>::fields()[0].offset == offsetof(CodeGenProbe, a),
    "model: field[0].offset must equal offsetof(Probe, a)");
static_assert(cacore::codegen::TypeDescriptor<CodeGenProbe>::fields()[0].kind == TypeKind::Int,
    "model: field[0].kind");
static_assert(cacore::codegen::TypeDescriptor<CodeGenProbe>::fields()[1].kind == TypeKind::Float,
    "model: field[1].kind");
static_assert(cacore::codegen::TypeDescriptor<CodeGenProbe>::fields()[2].kind == TypeKind::Bool,
    "model: field[2].kind");

// FieldFlags: per-member metadata is a bitmask one enum drives multiple consumers.
static_assert(cacore::codegen::has_flag(FieldFlags::Serializable | FieldFlags::HashInclude, FieldFlags::Serializable),
    "flags: OR then has_flag must see the bit");
static_assert(cacore::codegen::has_flag(FieldFlags::Serializable | FieldFlags::HashInclude, FieldFlags::HashInclude),
    "flags: OR then has_flag(HashInclude)");
static_assert(!cacore::codegen::has_flag(FieldFlags::Serializable | FieldFlags::HashInclude, FieldFlags::KeyMember),
    "flags: KeyMember bit must NOT be set");
static_assert(FieldFlags::None == static_cast<FieldFlags>(0), "flags: None must be zero");

// -----------------------------------------------------------------------------
// Task 1.2 — TypeDescriptor view + build-time ABI guard.
// The guard compares the generated fieldCount against CA_REFLECTION's member_count. If either the
// generated descriptor or the CA_REFLECTION changes so they disagree, the static_assert inside
// static_assert_abi_consistency fires => this file fails to compile (drift = build error).
// -----------------------------------------------------------------------------
static_assert(cacore::codegen::TypeDescriptor<CodeGenProbe>::fieldCount == ::CATypeDescriptor<CodeGenProbe>::member_count,
    "abi-guard precheck: generated 3 must equal CA_REFLECTION 3");
namespace { [[maybe_unused]] constexpr bool force_abi_guard_instantiation =
    (cacore::codegen::static_assert_abi_consistency<CodeGenProbe>(), true); }

// Task 2.1 — CA_CODEGEN_REGISTER("QualifiedName") expands to a greppable, references-free
// static_assert; including this TU (which calls the macro, deps: CACore) proves it compiles under
// MSVC. The qualification "CodeGenProbe" is the discovery string the CodeGen tool greps for.
