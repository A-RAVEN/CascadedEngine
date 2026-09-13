#pragma once
//
// CodeGenMeta.h — linear, data-driven metadata model for the engine CodeGen framework.
//
// Contrast with the existing macro/template reflection (`Reflection.h`): that system is
// fields-only + compile-time-only (member names live as CATypeMemberDesc tuple types, and
// the only runtime reader `log_reflection` is dead via an early `return;`). This header holds
// the *data-first* model that the CodeGen tool emits and reflection/serialization/hash emitters
// consume: plain arrays of structs, runtime-readable type/member names, readable at a breakpoint.
//
// It stays cargo-culted from `careflection` conventions (types prefixed CA_/CodeGen_ using
// `cacore::codegen`), but is NOT metaprogramming: generated data, not template tricks.
//
// Design ref: openspec/changes/engine-codegen-framework/design.md (D3 线性类型表, D4 ABI guard).

#include <cstddef>
#include <cstdint>
// The ABI guard compares generated metadata against the existing reflection descriptor, so we
// need its declarations (`::CATypeDescriptor<T>` + `careflection::has_type_desc<T>`).
#include "Reflection.h"

namespace cacore::codegen
{
    // Semantic kind of a field — drives serialization / hashing / C# marshal. NOT the spelled
    // C++ type; `typeName` carries that.
    enum class TypeKind : uint8_t
    {
        None   = 0,
        Bool,
        Int,           // signed integral (int8..int64)
        UInt,          // unsigned integral / enum underlying
        Float,         // float (normalized for -0.0/NaN in hash)
        Double,        // double
        Enum,          // scoped enum — value is blittable (4-byte handle enough for marshaling)
        Struct,        // aggregate — has nested fields (recursively reflected)
        String,        // length-prefixed text (variable length)
        Vector,        // fixed-size numeric vector (glm::vec)
        Mat,           // fixed-size numeric matrix (glm::mat)
        Array,         // fixed C array (extent known)
        Opaque,        // handle / pointer / shared_ptr / non-marshalled — opaquely hosted
    };

    // Per-member metadata flags: ONE enum drives multiple consumers (inspector widget kind,
    // C# marshal hint, serialization/hash include).
    enum class FieldFlags : uint32_t
    {
        None         = 0,
        Serializable = 1u << 0,   // include in [de]serialization
        HashInclude  = 1u << 1,   // include in semantic hash / equality
        KeyMember    = 1u << 2,   // part of the identity key (affects == in FullCompare mode)
    };

    [[nodiscard]] constexpr FieldFlags operator|(FieldFlags a, FieldFlags b) noexcept
    {
        return static_cast<FieldFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    [[nodiscard]] constexpr FieldFlags& operator|=(FieldFlags& a, FieldFlags b) noexcept
    {
        a = a | b;
        return a;
    }
    [[nodiscard]] constexpr bool has_flag(FieldFlags value, FieldFlags flag) noexcept
    {
        return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
    }

    // One member of a reflected aggregate. `name`/`typeName` are runtime-readable C strings
    // (stable across builds; drift becomes a regenerated build error, not silent corruption).
    struct CodeGenFieldInfo
    {
        const char*  name;      // member name, e.g. "model"
        const char*  typeName;  // spelled C++ type, e.g. "float" / "glm::mat4"
        std::size_t  offset;    // sizeof/alignment-stable byte offset (offsetof)
        TypeKind     kind;      // semantic kind (see TypeKind)
        FieldFlags   flags;     // per-member metadata (see FieldFlags)
    };

    // One reflected aggregate (struct/class). `fields` points at a contiguous, declaration-order
    // array of `fieldCount` CodeGenFieldInfo. The whole table is plain constexpr/static data.
    struct CodeGenTypeInfo
    {
        const char*               name;       // fully-qualified type name, e.g. "graphics_backend::ImageHandle"
        TypeKind                  kind;       // Struct for aggregates
        std::size_t               fieldCount; // number of `fields`
        const CodeGenFieldInfo*   fields;     // declaration-order array
        // Future emitters may add: methods() / enumInfo() / baseClass() / attributes().
    };

    // ------------------------------------------------------------------------------------
    // Emitter output view. The CodeGen reflection emitter specializes this for each annotated
    // type, producing constexpr static data (a linear array) instead of a std::tuple of template
    // types. Parallel in intent to `careflection::CATypeDescriptor<T>` (Reflection.h:58/82-99),
    // but data-first and runtime-readable.
    // ------------------------------------------------------------------------------------
    template<typename T>
    struct TypeDescriptor
    {
        // Default: no generated metadata (Null descriptor). The emitter provides the real one.
        static constexpr const char*         name       = nullptr;
        static constexpr std::size_t         fieldCount = 0;
        static constexpr const CodeGenFieldInfo* fields() { return nullptr; }
    };

    // Build-time ABI guard. What it ACTUALLY asserts (and nothing more): the generated descriptor's
    // field count equals `careflection::CATypeDescriptor<T>::member_count`, plus a non-null check on the
    // generated type name (an "is the generated header included at all" test — it does NOT compare the
    // name's CONTENT, and it does NOT compare offsets). Drift in the field count → build error, catching
    // stale generated output against the source-of-truth reflection before it ships. Field-name or offset
    // drift is NOT caught here: names are checked at runtime (`strcmp` in the consumer test) and offsets
    // are resolved by the generated code's own `offsetof`, i.e. by the compiler, at the point of use.
    // (Silently passes for types that are NOT CA_REFLECTION'd — nothing to compare against.)
    template<typename T>
    constexpr void static_assert_abi_consistency() noexcept
    {
        if constexpr (careflection::has_type_desc<T>)
        {
            static_assert(
                TypeDescriptor<T>::fieldCount == ::CATypeDescriptor<T>::member_count,
                "CodeGen ABI drift: generated field count != CA_REFLECTION member count");
            static_assert(
                TypeDescriptor<T>::name != nullptr,
                "CodeGen ABI drift: generated type has no name");
        }
    }

} // namespace cacore::codegen

// ---------------------------------------------------------------------------------------------
// Discovery macro. The engine compiles with MSVC, so we deliberately do NOT use literal
// `[[careflection::...]]` / `[[clang::annotate]]` attributes: MSVC warns C5030 for unknown-attribute
// namespaces (verified: non-fatal under /WX-, but noisy per-use), and libclang's C API reads
// custom-namespaced attributes only weakly (llvm discourse 62083) / `[[clang::annotate]]` can be
// invisible in the AST after macro expansion (llvm#87813). Instead this is a plain, greppable,
// toolchain-agnostic on-ramp for "which types cross the boundary".
// The argument is a STRING LITERAL of the fully-qualified type name (so `::`-qualified names are
// fine — no identifier token-pasting). It expands to an always-true static_assert whose message
// embeds the name, giving the CodeGen tool a stable greppable token it extracts at build time, and
// compiling as a zero-cost no-op under MSVC. Design D2: `CA_REFLECT(...)` (with options) will be
// the forward marker; this is its minimal no-options precursor.
// ---------------------------------------------------------------------------------------------
#define CA_CODEGEN_REGISTER(QualifiedName) \
    static_assert(true, "CA_CODEGEN_REGISTER:" QualifiedName)

// ---------------------------------------------------------------------------------------------
// CA_REFLECT — the FORWARD marker for engine-owned types (design D2). Attribute-style: takes an
// options list (e.g. serializable, ui, min=0, marshal(as=...)) but NO type name — the associated
// type is determined by the CodeGen tool as the class body FOLLOWING this marker (textual
// association; the marker is a no-op that carries its options as text for grep). MSVC-safe no-op
// (always-true static_assert), greppable via the CA_REFLECT( token. NOT a literal [[...]] attribute.
// ---------------------------------------------------------------------------------------------
#define CA_REFLECT(...) \
    static_assert(true, "CA_REFLECT(" #__VA_ARGS__ ")");
