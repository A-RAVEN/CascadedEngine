#pragma once
//
// CodeGenMeta.h — linear, data-driven metadata model for the engine CodeGen framework.
//
// The model is *data-first*: plain arrays of structs holding runtime-readable type/member names and
// byte offsets, readable at a breakpoint. The CodeGen tool emits these tables; the reflection /
// serialization / hash emitters consume them.
//
// It stays cargo-culted from `careflection` conventions (types prefixed CA_/CodeGen_ using
// `cacore::codegen`), but is NOT metaprogramming: generated data, not template tricks.
//
// Design ref: openspec/changes/archive/2026-09-20-engine-codegen-framework/design.md (D3 线性类型表, D4 ABI guard).

#include <cstddef>
#include <cstdint>

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
        Container    = 1u << 3,   // the member is a container (vector / map / set / string / array …)
        Unordered    = 1u << 4,   // …whose traversal order is unspecified (unordered_map / unordered_set)
        Wrapper      = 1u << 5,   // the member (or its element) is a wrapper — recorded, NOT a refusal
        HasOpaqueElement = 1u << 6 // element is a raw pointer / managed pointer / handle
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

    // One member of a reflected aggregate. `name`/`typeName` are runtime-readable C strings.
    // ⚠ Honest scope (2026-09-13): what catches drift is the build-time byte-comparison of each generated
    // header against its committed golden (build.py), plus the field-count ABI guard below. A changed
    // member NAME is not caught at compile time at all — the only name check is a runtime `strcmp` in the
    // consumer test; offsets are resolved by the generated code's own `offsetof`, i.e. by the compiler.
    struct CodeGenFieldInfo
    {
        const char*  name;      // member name, e.g. "model"
        const char*  typeName;  // spelled C++ type, e.g. "float" / "glm::mat4"
        std::size_t  offset;    // sizeof/alignment-stable byte offset (offsetof)
        TypeKind     kind;      // semantic kind (see TypeKind)
        FieldFlags   flags;     // per-member metadata (see FieldFlags)
        // Container members only (FieldFlags::Container): the element description (for a map, the
        // MAPPED type) and, for maps, the key description. Both point at a one-entry static table the
        // emitter generates alongside; their `offset` is 0 because they are not members of the parent
        // type. Non-container members leave both nullptr.
        const CodeGenFieldInfo* key     = nullptr;
        const CodeGenFieldInfo* element = nullptr;
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
    // type, producing constexpr static data (a linear array) instead of a tuple of template
    // types: data-first and runtime-readable.
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
    // type name is non-null (an "is the generated header included at all" test —— it does NOT compare
    // the name's CONTENT, and it does NOT compare offsets). Field-count / field-name / offset drift is
    // checked by the consumer-unit assertion files, which compare against written-down expected values
    // (`CPipelineStateObjectAssertions.cpp`, the two `ShaderLibraryCodeGenAssertions.cpp`): those fail
    // to compile when the generated table and the expectation disagree.
    template<typename T>
    constexpr void static_assert_abi_consistency() noexcept
    {
        static_assert(
            TypeDescriptor<T>::name != nullptr,
            "CodeGen ABI drift: generated type has no name (the generated header is not visible here)");
    }

} // namespace cacore::codegen

// ---------------------------------------------------------------------------------------------
// 私有成员的友元入口。老系统里对应的是一个只友元旧描述符模板的宏（`CA_PRIVATE_REFLECTION`）；
// 新系统有四个入口，所以四条都要友元 —— 生成代码是按成员名直接读写的，少给哪一个，那个发射器的产物
// 就会在消费单元里被访问权挡住（C2248）。
//
// 用法：写在**类体里面**（和 `friend` 声明一样的位置），例如
//     CA_REFLECT(serializable)
//     class ImageHandle
//     {
//         CA_CODEGEN_FRIEND(ImageHandle);
//         uint64_t m_Id;
//     };
// 工具读的是 AST 里的 friend 声明（`CXCursor_FriendDecl`），不做源码文本扫描；类里没有这一行时，
// 含私有成员的类型在发射时仍然以退出码 5 拒绝。
//
// 三个前置声明只为让 `friend struct ...<Type>;` 合法；它们的定义分别在 CodeGenSerialization.h /
// CodeGenHash.h / CodeGenHashFwd.h，模板参数表与这里逐字一致。
namespace cacore::codegen
{
    template<typename T> struct CodeGenSerializer;
    template<typename T> struct CodeGenEqual;
    template<typename T> struct custom_hash_trait;
} // namespace cacore::codegen

#define CA_CODEGEN_FRIEND(Type)                                     \
    friend struct ::cacore::codegen::TypeDescriptor<Type>;          \
    friend struct ::cacore::codegen::CodeGenSerializer<Type>;       \
    friend struct ::cacore::codegen::CodeGenEqual<Type>;            \
    friend struct ::cacore::codegen::custom_hash_trait<Type>

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
// compiling as a zero-cost no-op under MSVC. `CA_REFLECT(...)` (with options) will be
// the forward marker; this is its minimal no-options precursor.
// ---------------------------------------------------------------------------------------------
#define CA_CODEGEN_REGISTER(QualifiedName) \
    static_assert(true, "CA_CODEGEN_REGISTER:" QualifiedName)

// ---------------------------------------------------------------------------------------------
// CA_REFLECT — the FORWARD marker for engine-owned types. Attribute-style: takes an
// options list (e.g. serializable, ui, min=0, marshal(as=...)) but NO type name — the associated
// type is determined by the CodeGen tool as the class body FOLLOWING this marker (textual
// association; the marker is a no-op that carries its options as text for grep). MSVC-safe no-op
// (always-true static_assert), greppable via the CA_REFLECT( token. NOT a literal [[...]] attribute.
// ---------------------------------------------------------------------------------------------
#define CA_REFLECT(...) \
    static_assert(true, "CA_REFLECT(" #__VA_ARGS__ ")");

// ---------------------------------------------------------------------------------------------
// CA_BIND — 标在**接口类**上，告诉 CodeGen 的绑定发射器"这个接口要给 C# 用"（规矩见
// openspec/specs/csharp-bindable-surface/spec.md 与 Tools/CodeGen/README.md 第十三节）。
// 与 CA_REFLECT 一样是"文本标记 + 编译期 no-op"：工具按源码文本认它，编译器这里什么也不做。
//
// CA_BIND_SKIP(方法名, "理由") —— 逐方法排除。它**必须带理由**：工具对"排除但没写理由"单独报错。
// 用法与退出码见 Tools/CodeGen/README.md 的发射器一节。
// ---------------------------------------------------------------------------------------------
#define CA_BIND(...) \
    static_assert(true, "CA_BIND(" #__VA_ARGS__ ")");

#define CA_BIND_SKIP(MethodName, Reason) \
    static_assert(true, "CA_BIND_SKIP(" #MethodName ", " Reason ")");
