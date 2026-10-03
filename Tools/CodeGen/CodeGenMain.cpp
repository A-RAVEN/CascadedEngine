//
// CodeGenMain.cpp — minimal libclang-driven extraction tool for the CodeGen framework.
//
// Takes a header file + a type name, parses it with libclang (the real C++ front end — we are
// NOT hand-writing a parser), finds the struct/class of that name, and emits a deterministic
// linear field table (one line per field, in DECLARATION order — task 9.5a removed the sort by
// extracted byte offset, which was built on a value a degenerate environment silently collapses to 0):
//
//     field <name> <offsetBytes> <typeSpelling> <CXTypeKind>
//
// This is the proof-of-pipeline for task 3.1/3.2: parse -> cursor traversal -> type table, and
// the output is deterministic (same input => same output, so re-emission is byte-identical).
// Later emitters (reflection/serialization/hash) consume this table to generate C++ / C# code.
//
// Usage: CodeGen <file.h> <TypeName> [clang-arg ...]   (e.g. "-I<dir>", "-std=c++20", "-xc++")

#include <clang-c/Index.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct Field
    {
        std::string name;
        std::string typeName;
        std::size_t offsetBytes;
        int         typeKind; // CXTypeKind
        // Task 2.1: container facts, recognized structurally (see analyzeContainer).
        bool        isContainer = false;
        bool        containerUnordered = false;
        bool        containerIsMap = false;
        bool        containerIsSet = false;
        bool        containerIsMultiKey = false;
        bool        containerIsBitPacked = false;
        std::string keyTypeName;          // map only
        int         keyKind = 0;          // map only
        std::string elementTypeName;      // direct element type spelling (map: the mapped type)
        int         elementKind = 0;      // CXTypeKind of the (canonicalized) element
        bool        elementIsContainer = false;
        bool        elementIsOpaque = false;         // raw pointer / handle-ish
        bool        elementIsManagedPointer = false; // shared_ptr / unique_ptr
        // Task 3.2 (1): non-empty when the element is a STRUCT whose own fields hold an unordered
        // container that the generated recursion cannot reach. "<成员名> (<容器名>)".
        std::string elementUnstableInside;
        bool        elementIsWrapper = false;   // Task 2.2: recorded only, never opaque
        bool        keyNotDefault = false;       // Task 4.1 (4)
        bool        sortSideNotOrdered = false; // Task 4.1 (1)
        bool        elementNotDefault = false;
        // Task 2.2: for `int a[2][3]` the element is `int[3]`, and THIS is its own element (`int`).
        std::string element2TypeName;
        int         element2Kind = 0;
        bool        element2IsContainer = false;
        bool        keyIsWrapper = false;
    };

    struct CollectCtx
    {
        std::vector<std::pair<std::string, CXCursor>>* out;
    };

    /** Last path component — lets us compare file spellings that may be absolute or relative. */
    std::string baseName(const std::string& path)
    {
        const std::size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    /**
     * libclang keeps an elaborated-type wrapper around class template specializations even AFTER
     * clang_getCanonicalType (measured: `std::vector<unsigned>` reports CXType_Elaborated 105 with
     * canonicalization, so a plain `kind == CXType_Record` test never fires and every container looked
     * like a non-container). Strip that wrapper — and any attributed wrapper — before asking questions
     * about the record.
     */
    CXType stripElaborated(CXType t)
    {
        for (int i = 0; i < 8; ++i)
        {
            if (t.kind == CXType_Elaborated)
                t = clang_Type_getNamedType(t);
            else if (t.kind == CXType_Attributed)
                t = clang_Type_getModifiedType(t);
            else
                break;
        }
        return t;
    }

    /** Does the record declare a member with this name (method, typedef, field — any kind)? */
    bool recordHasMember(CXType record, const char* name)
    {
        const CXCursor decl = clang_getTypeDeclaration(record);
        if (clang_Cursor_isNull(decl))
            return false;
        struct Ctx
        {
            const char* want;
            bool        found;
        } ctx{ name, false };
        clang_visitChildren(
            decl,
            [](CXCursor c, CXCursor, CXClientData data) -> CXChildVisitResult {
                auto* ctx = static_cast<Ctx*>(data);
                CXString sp = clang_getCursorSpelling(c);
                const char* s = clang_getCString(sp);
                if (s != nullptr && std::strcmp(s, ctx->want) == 0)
                    ctx->found = true;
                clang_disposeString(sp);
                // RECURSE, not Continue: for a class template specialization the members live inside
                // the templated class declaration that hangs under the template declaration, so a
                // non-recursing visit only ever saw the template parameters (measured: hasSize=0 for
                // `std::vector<unsigned>` even though its canonical type IS a CXType_Record).
                return ctx->found ? CXChildVisit_Break : CXChildVisit_Recurse;
            },
            &ctx);
        return ctx.found;
    }

    bool looksLikeManagedPointer(CXType canonical)
    {
        CXString sp = clang_getTypeSpelling(canonical);
        const char* s = clang_getCString(sp);
        const bool managed = (s != nullptr) &&
                             (std::strstr(s, "shared_ptr") != nullptr || std::strstr(s, "unique_ptr") != nullptr);
        clang_disposeString(sp);
        return managed;
    }

    /** Base name of a canonical type spelling: `std::vector<unsigned int>` -> `vector`. */
    std::string canonicalBaseName(CXType canon)
    {
        CXString sp = clang_getTypeSpelling(canon);
        const char* c = clang_getCString(sp);
        std::string s = (c != nullptr) ? c : "";
        clang_disposeString(sp);
        const std::size_t lt = s.find('<');
        if (lt != std::string::npos)
            s = s.substr(0, lt);
        const std::size_t colon = s.rfind("::");
        if (colon != std::string::npos)
            s = s.substr(colon + 2);
        while (!s.empty() && (s.back() == ' ' || s.back() == '>'))
            s.pop_back();
        return s;
    }

    /**
     * Task 2.1: containers are recognized STRUCTURALLY — exactly the rule the runtime uses
     * (`is_size_container`: a `size()` member plus a `value_type`), never by container name, because
     * `castl::X` reaches every `std::` / `eastl::` name through `using namespace`.
     * `bucket_count()` marks the unordered ones; `mapped_type` marks maps. Nothing here reads the
     * source text: all of it is the canonical type's own declaration.
     *
     * ⚠ MEASURED LIMIT (2026-10-02): probing the members with libclang cursors does NOT work here —
     * `clang_getTypeDeclaration` on a canonical class-template specialization does not expose its
     * members (`recordHasMember` returned 0 for `std::vector<unsigned>` even when the visitor recursed,
     * and the canonical kind is CXType_Record 105, i.e. the type itself is fine). So the recognition is
     * a list of canonical BASE NAMES (`vector`, `map`, `unordered_set`, …), which is stable because the
     * canonical spelling is `std::…` / `eastl::…`, never the `castl::…` alias. The runtime-side rule is
     * kept aligned by the parity assertion in the tests (task 2.5), not by this probe.
     */
    const char* const kContainerNames[] = {
        "vector", "array", "string", "basic_string", "deque", "list",
        "map", "multimap", "set", "multiset",
        "unordered_map", "unordered_multimap", "unordered_set", "unordered_multiset",
    };

    bool isContainerType(CXType canonical)
    {
        const CXType t = stripElaborated(canonical);
        if (t.kind != CXType_Record)
            return false;
        const std::string base = canonicalBaseName(t);
        for (const char* n : kContainerNames)
            if (base == n)
                return true;
        return false;
    }

    /* Task 4.1 (1): does the type carry an ordering operator? The canonical write order for an unordered
     * container needs a strict weak order on the sorting side. `operator<=>` counts: the engine's real
     * key types (NameHash, PathHash, CAHash::result_type) define ONLY `<=>` and no `operator<`, so a
     * criterion that insists on `operator<` would reject every real key type.
     */
    struct OrderWalk
    {
        bool found = false;
    };

    CXChildVisitResult orderWalkVisitor(CXCursor c, CXCursor, CXClientData data)
    {
        auto* w = static_cast<OrderWalk*>(data);
        CXString sp = clang_getCursorSpelling(c);
        const char* sc = clang_getCString(sp);
        const std::string name = (sc != nullptr) ? sc : "";
        clang_disposeString(sp);
        if (name.rfind("operator", 0) == 0 && name.find('<') != std::string::npos ||
            name.rfind("operator", 0) == 0 && name.find('>') != std::string::npos)
        {
            w->found = true;
            return CXChildVisit_Break;
        }
        return CXChildVisit_Continue;
    }

    bool hasOrderingOperator(CXType type)
    {
        const CXType canon = stripElaborated(clang_getCanonicalType(type));
        if (canon.kind != CXType_Record)
            return true;                     // scalars, pointers, enums all compare
        if (isContainerType(canon))
            return true;                     // the container brings its own comparator
        const CXCursor decl = clang_getTypeDeclaration(canon);
        if (clang_Cursor_isNull(decl))
            return true;                     // cannot inspect -> do not refuse
        OrderWalk w;
        clang_visitChildren(decl, orderWalkVisitor, &w);
        return w.found;
    }

    /* Task 4.1 (4): can a value of this type be built with `T t{}`? The generated reader does exactly
     * that before reading into it, so a key / element type without a usable default constructor would
     * make the emitted code fail to compile. Records that cannot be inspected are assumed fine.
     */
    struct CtorWalk
    {
        bool hasAny     = false;
        bool hasDefault = false;
    };

    CXChildVisitResult ctorWalkVisitor(CXCursor c, CXCursor, CXClientData data)
    {
        auto* w = static_cast<CtorWalk*>(data);
        if (clang_getCursorKind(c) == CXCursor_Constructor)
        {
            w->hasAny = true;
            if (clang_Cursor_getNumArguments(c) == 0 && !clang_CXXMethod_isDeleted(c))
                w->hasDefault = true;
        }
        return CXChildVisit_Continue;
    }

    bool isDefaultConstructible(CXType type)
    {
        const CXType canon = stripElaborated(clang_getCanonicalType(type));
        if (canon.kind != CXType_Record)
            return true;
        if (isContainerType(canon))
            return true;
        const CXCursor decl = clang_getTypeDeclaration(canon);
        if (clang_Cursor_isNull(decl))
            return true;
        CtorWalk w;
        clang_visitChildren(decl, ctorWalkVisitor, &w);
        return !w.hasAny || w.hasDefault;
    }

    /* Task 2.2: the second level of a nested description. For `vector<vector<int>>` the element is
     * `vector<int>`; this fills in that container's OWN element (`int`), so the table expresses two
     * levels. For maps the value type is the element (index 1), for everything else index 0.
     */
    void describeElementOfContainer(CXType container, std::string& typeName, int& kind, bool& isContainer)
    {
        const CXType c    = stripElaborated(clang_getCanonicalType(container));
        const int    argc = clang_Type_getNumTemplateArguments(c);
        if (argc <= 0)
            return;
        const std::string base = canonicalBaseName(c);
        const int         idx  = (base.find("map") != std::string::npos && argc >= 2) ? 1 : 0;
        const CXType      arg  = clang_Type_getTemplateArgumentAsType(c, idx);
        CXString          sp   = clang_getTypeSpelling(arg);
        const char*       sc   = clang_getCString(sp);
        typeName               = (sc != nullptr) ? sc : "";
        clang_disposeString(sp);
        kind        = static_cast<int>(stripElaborated(clang_getCanonicalType(arg)).kind);
        isContainer = isContainerType(arg);
    }

    /* Task 2.2: the engine's wrappers. Detection is BY NAME because what makes them wrappers -- the old
     * runtime's `managed_wrapper_traits` specialization -- is not visible to libclang. The three names are
     * the ones the design names (D3); a new wrapper type has to be added here, which is a known boundary.
     */
    const char* const kWrapperNames[] = { "NameHash", "PathHash", "HashObj" };

    bool looksLikeWrapper(CXType type)
    {
        CXString    sp   = clang_getTypeSpelling(stripElaborated(clang_getCanonicalType(type)));
        const char* c    = clang_getCString(sp);
        std::string name = (c != nullptr) ? c : "";
        clang_disposeString(sp);
        for (const char* n : kWrapperNames)
        {
            const std::string part = std::string("::") + n;
            if (name.rfind(n, 0) == 0 || name.find(part) != std::string::npos)
                return true;
        }
        return false;
    }

    std::string findUnorderedInside(CXType type, int depth);

    struct UnorderedWalk
    {
        std::string found;
    };

    CXChildVisitResult unorderedWalkVisitor(CXCursor c, CXCursor, CXClientData data)
    {
        auto* w = static_cast<UnorderedWalk*>(data);
        if (!w->found.empty())
            return CXChildVisit_Break;
        if (clang_getCursorKind(c) != CXCursor_FieldDecl)
            return CXChildVisit_Continue;
        const std::string hit = findUnorderedInside(clang_getCursorType(c), 1);
        if (!hit.empty())
        {
            CXString n = clang_getCursorSpelling(c);
            const char* nc = clang_getCString(n);
            w->found = std::string((nc != nullptr) ? nc : "?") + " (" + hit + ")";
            clang_disposeString(n);
            return CXChildVisit_Break;
        }
        return CXChildVisit_Continue;
    }

    /* Task 3.2 (1): can the generated code reproduce this element byte-for-byte? Only when every
     * unordered container inside it is reachable by the generated recursion. A STRUCT element's fields
     * are not reachable: the generated reader has no way to continue another type's generated reader
     * (measured -- Serialization.h keeps `serializer::buffer` and `deserializer::m_Offset` private and
     * offers no offset accessor), so such an element would still be written in traversal order. This
     * returns the offending field, e.g. "m (unordered_map)", or "" when nothing was found. Records
     * whose declaration is not in this translation unit cannot be inspected -- that limit is written
     * down in the design's known-boundary list.
     */
    std::string findUnorderedInside(CXType type, int depth)
    {
        if (depth > 3)
            return "";
        CXType canon = stripElaborated(clang_getCanonicalType(type));
        if (canon.kind == CXType_ConstantArray || canon.kind == CXType_IncompleteArray)
            return findUnorderedInside(clang_getArrayElementType(canon), depth + 1);
        if (canon.kind != CXType_Record)
            return "";
        const std::string base = canonicalBaseName(canon);
        if (base.rfind("unordered_", 0) == 0)
            return base;
        const CXCursor decl = clang_getTypeDeclaration(canon);
        if (clang_Cursor_isNull(decl))
            return "";
        UnorderedWalk w;
        clang_visitChildren(decl, unorderedWalkVisitor, &w);
        return w.found;
    }

    void analyzeContainer(CXType spelled, Field& f)
    {
        const CXType canon = stripElaborated(clang_getCanonicalType(spelled));
        if (canon.kind == CXType_ConstantArray || canon.kind == CXType_IncompleteArray)
        {
            // Task 2.2 / 3.2 (3): a C array is a container by EXTENT. Its element is the type with one
            // dimension removed, so `int a[2][3]` reports `int[3]`, and recursion reaches `int`.
            f.isContainer             = true;
            const CXType   element    = clang_getArrayElementType(canon);
            CXString       elementSp  = clang_getTypeSpelling(element);
            const char*    elementC   = clang_getCString(elementSp);
            f.elementTypeName         = (elementC != nullptr) ? elementC : "";
            clang_disposeString(elementSp);
            const CXType elementCanon = stripElaborated(clang_getCanonicalType(element));
            f.elementKind             = static_cast<int>(elementCanon.kind);
            f.elementIsContainer      = (elementCanon.kind == CXType_ConstantArray) ||
                                        (elementCanon.kind == CXType_IncompleteArray) ||
                                        isContainerType(elementCanon);
            if (elementCanon.kind == CXType_ConstantArray || elementCanon.kind == CXType_IncompleteArray)
            {
                const CXType inner   = clang_getArrayElementType(elementCanon);
                CXString     innerSp = clang_getTypeSpelling(inner);
                const char*  innerC  = clang_getCString(innerSp);
                f.element2TypeName   = (innerC != nullptr) ? innerC : "";
                clang_disposeString(innerSp);
                f.element2Kind = static_cast<int>(stripElaborated(clang_getCanonicalType(inner)).kind);
            }
            f.elementIsOpaque         = (elementCanon.kind == CXType_Pointer);
            if (!f.elementIsContainer && elementCanon.kind == CXType_Record)
                f.elementUnstableInside = findUnorderedInside(elementCanon, 0);
            return;
        }
        const std::string base = canonicalBaseName(canon);
        bool known = false;
        for (const char* n : kContainerNames)
            if (base == n) { known = true; break; }
        if (!known)
            return;
        f.isContainer         = true;
        f.containerUnordered  = (base.rfind("unordered_", 0) == 0);
        f.containerIsMap      = (base.find("map") != std::string::npos);
        f.containerIsSet      = (base.find("set") != std::string::npos);
        f.containerIsMultiKey = (base.find("multi") != std::string::npos);

        const int argCount = clang_Type_getNumTemplateArguments(canon);
        if (argCount <= 0)
            return;
        const auto argAt = [canon](int i) {
            const CXType spelledArg = clang_Type_getTemplateArgumentAsType(canon, i);
            CXString     sp         = clang_getTypeSpelling(spelledArg);
            const char*  c          = clang_getCString(sp);
            std::string  name       = (c != nullptr) ? c : "";
            clang_disposeString(sp);
            return std::make_pair(name, stripElaborated(clang_getCanonicalType(spelledArg)));
        };

        if (f.containerIsMap && argCount >= 2)
        {
            const auto k = argAt(0);
            const auto v = argAt(1);
            f.keyTypeName = k.first;
            f.keyKind     = static_cast<int>(k.second.kind);
            f.elementTypeName = v.first;
            f.elementKind     = static_cast<int>(v.second.kind);
            f.elementIsContainer = isContainerType(v.second);
            f.keyIsWrapper     = looksLikeWrapper(k.second);
            f.elementIsWrapper = looksLikeWrapper(v.second);
            f.elementNotDefault = !isDefaultConstructible(v.second);
            if (f.containerUnordered && !hasOrderingOperator(v.second))
                f.sortSideNotOrdered = true;
            if (f.containerUnordered && !hasOrderingOperator(k.second))
                f.sortSideNotOrdered = true;
            f.keyNotDefault     = !isDefaultConstructible(k.second);
            if (f.elementIsContainer)
            {
                bool secondIsContainer = false;
                describeElementOfContainer(v.second, f.element2TypeName, f.element2Kind, secondIsContainer);
                f.element2IsContainer = secondIsContainer;
            }
            if (!f.elementIsContainer && v.second.kind == CXType_Record)
                f.elementUnstableInside = findUnorderedInside(v.second, 0);
        }
        else
        {
            const auto e = argAt(0);
            f.elementTypeName = e.first;
            f.elementKind     = static_cast<int>(e.second.kind);
            f.elementIsContainer = isContainerType(e.second);
            f.elementIsWrapper = looksLikeWrapper(e.second);
            f.elementNotDefault = !isDefaultConstructible(e.second);
            if (f.containerUnordered && !hasOrderingOperator(e.second))
                f.sortSideNotOrdered = true;
            if (f.elementIsContainer)
            {
                bool secondIsContainer = false;
                describeElementOfContainer(e.second, f.element2TypeName, f.element2Kind, secondIsContainer);
                f.element2IsContainer = secondIsContainer;
            }
            if (!f.elementIsContainer && e.second.kind == CXType_Record)
                f.elementUnstableInside = findUnorderedInside(e.second, 0);
            // `vector<bool>` is the bitset-backed odd one out: its elements are proxies, so the
            // runtime's `for (auto& item : object)` cannot be instantiated (measured MSVC C2440).
            if (base == "vector" && e.second.kind == CXType_Bool)
                f.containerIsBitPacked = true;
        }
        f.elementIsOpaque         = (f.elementKind == CXType_Pointer);
        f.elementIsManagedPointer = looksLikeManagedPointer(clang_getCanonicalType(
            clang_Type_getTemplateArgumentAsType(canon, f.containerIsMap ? 1 : 0)));
    }

    enum CXChildVisitResult collectStructs(CXCursor cursor, CXCursor /*parent*/, CXClientData data)
    {
        auto* ctx  = static_cast<CollectCtx*>(data);
        CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl)
        {
            // Task 9.57: only types declared at namespace scope (or at file scope) are candidates. The
            // visitor recurses into functions, so a helper struct declared INSIDE a function used to be
            // pooled with the namespace-level type of the same name; the tool then emitted the local
            // struct's member list for the annotated global type (measured: `Foo 1 / field inner` for a
            // global `Foo { int a; int b; }` that carried the annotation).
            const CXCursorKind parentKind = clang_getCursorKind(clang_getCursorSemanticParent(cursor));
            if (parentKind != CXCursor_TranslationUnit && parentKind != CXCursor_Namespace)
                return CXChildVisit_Continue;
            CXString spelling = clang_getCursorSpelling(cursor);
            ctx->out->emplace_back(clang_getCString(spelling), cursor);
            clang_disposeString(spelling);
            return CXChildVisit_Continue; // do not recurse into nested decls (top-level sufficient)
        }
        return CXChildVisit_Recurse;
    }

    struct FieldSink
    {
        std::vector<Field>* fields       = nullptr;
        // Task 9.1: the OUTER-class annotation channel (CA_REFLECTION / CA_REFLECT written at namespace
        // scope) is a zero-intrusion text scan; the code it emits names members directly, so it can only
        // serve types whose members are ALL public. Extraction (printing the type table) is still fine for
        // private members — only EMITTING compilable code is refused. See the guard before the emitters.
        bool                sawNonPublic = false;
    };

    enum CXChildVisitResult collectFields(CXCursor cursor, CXCursor /*parent*/, CXClientData data)
    {
        auto* sink = static_cast<FieldSink*>(data);
        if (clang_getCursorKind(cursor) == CXCursor_FieldDecl)
        {
            CXString sName = clang_getCursorSpelling(cursor);
            CXType   type  = clang_getCursorType(cursor);
            CXString sType = clang_getTypeSpelling(type);
            long long bits = clang_Cursor_getOffsetOfField(cursor);

            // CX_CXXInvalidAccessSpecifier is deliberately NOT treated as non-public: it shows up for
            // constructs libclang does not classify, and the fixtures' public struct members must not
            // trip the guard (verified by the 9.1 oracle's regression half).
            const CX_CXXAccessSpecifier access = clang_getCXXAccessSpecifier(cursor);
            if (access == CX_CXXPrivate || access == CX_CXXProtected)
                sink->sawNonPublic = true;

            Field f;
            f.name       = clang_getCString(sName);
            f.typeName   = clang_getCString(sType);
            f.offsetBytes = bits > 0 ? static_cast<std::size_t>(bits) / 8u : 0u;
            f.typeKind   = static_cast<int>(clang_getCanonicalType(type).kind);
            analyzeContainer(type, f);   // Task 2.1: container facts, recognized structurally

            clang_disposeString(sName);
            clang_disposeString(sType);
            sink->fields->push_back(std::move(f));
        }
        return CXChildVisit_Continue;
    }

    // --- Task 6b.2: read CA_REFLECT marker options from the source TEXT (not libclang attributes).
    // libclang's C API reads custom-namespaced attributes only weakly, so the marker macro's option
    // list travels as text for the tool to grep. The associated type is the class body FOLLOWING the
    // nearest preceding `CA_REFLECT(...)` line (textual association). Deterministic.
    std::string readFile(const char* path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    std::string trim(const std::string& s)
    {
        std::size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos)
            return {};
        std::size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    /** Task 9.42 + 9.50②: match a marker option as a WHOLE `name[=value]` pair.
        - the old check was `opts.find("serializable")` (substring: `nonserializable` passed, a mention
          inside a comment passed);
        - `serializable=0` / `serializable=false` must NOT count as requesting serialization — the value
          is now parsed and only a truthy one is a hit. */
    bool hasMarkerOption(const std::string& options, const std::string& want)
    {
        std::string s;
        bool inLine = false, inBlock = false;
        for (std::size_t i = 0; i < options.size(); ++i)
        {
            const char c = options[i];
            const char n = (i + 1 < options.size()) ? options[i + 1] : '\0';
            if (inLine) { if (c == '\n') inLine = false; continue; }
            if (inBlock) { if (c == '*' && n == '/') { inBlock = false; ++i; } continue; }
            if (c == '/' && n == '/') { inLine = true; ++i; continue; }
            if (c == '/' && n == '*') { inBlock = true; ++i; continue; }
            s.push_back(c);
        }
        std::size_t start = 0;
        while (start <= s.size())
        {
            const std::size_t comma = s.find(',', start);
            std::string tok = trim(s.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start));
            const std::size_t eq = tok.find('=');
            std::string name = (eq == std::string::npos) ? tok : trim(tok.substr(0, eq));
            const std::string value = (eq == std::string::npos) ? std::string() : trim(tok.substr(eq + 1));
            if (name == want)
            {
                if (eq == std::string::npos)
                    return true;                       // bare flag
                std::string v = value;
                for (char& ch : v) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return v == "true" || v == "1" || v == "yes" || v == "on";
            }
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        return false;
    }

    /** Task 9.59: WHICH TYPES ARE ANNOTATED is now answered by the preprocessor, not by scanning text.

        Three review rounds in a row (tasks 9.50, 9.54, 9.55, 9.57) each closed one spelling by which a
        text scan could be fooled and each time a neighbouring spelling reopened the same defect class:
        a marker inside a comment, then inside a string, then inside a line-spliced comment, then inside
        disabled preprocessor branches, then inside a `#define` body, then `#if 0` with a trailing
        comment, then a `classify` identifier that made the scanner see `class`. The lesson recorded in
        docs/failed-approaches.md rows 17, 18 and 20 is that the class cannot be closed by enumeration.

        So the tool no longer decides what is code. libclang's preprocessor does, and we read its answer:
        with CXTranslationUnit_DetailedPreprocessingRecord the translation unit yields one
        CXCursor_MacroExpansion per macro INVOCATION that the preprocessor actually performed. That set is
        empty for a marker inside a comment, a string, a disabled branch or an unused `#define` body, and
        it is exactly the set of annotations the compiler itself saw. The only text we still read is the
        byte range of a range clang handed us, i.e. the contents of a real invocation. */

    std::string unqualifiedName(const std::string& s); // defined with the gate helpers

    struct AnnotationUse
    {
        std::string macro; // CA_REFLECT / CA_CODEGEN_REGISTER / CA_REFLECTION
        std::string args;  // invocation text between the first '(' and the matching ')'
        std::size_t begin = 0;
        std::size_t end   = 0; // one past the invocation
    };

    struct TypeOccurrence
    {
        std::size_t offset = 0; // byte offset of the declaration in the parsed file
        std::string name;
        CXCursor    cursor{};
        bool        isDefinition = false;
    };

    struct AnnotationCtx
    {
        const char*                  filePath;
        std::vector<AnnotationUse>*  uses;
        std::vector<TypeOccurrence>* types;
    };

    /** True when a cursor comes from the file the tool was asked to parse (not an included header). */
    bool cursorIsInMainFile(CXCursor cursor, const char* filePath)
    {
        CXFile file = nullptr;
        unsigned line = 0, column = 0, offset = 0;
        clang_getFileLocation(clang_getCursorLocation(cursor), &file, &line, &column, &offset);
        if (!file)
            return false;
        CXString name = clang_getFileName(file);
        const char* cname = clang_getCString(name);
        const bool inMain = cname != nullptr && std::strstr(cname, filePath) != nullptr;
        clang_disposeString(name);
        return inMain;
    }

    enum CXChildVisitResult collectAnnotations(CXCursor cursor, CXCursor /*parent*/, CXClientData data)
    {
        auto* ctx = static_cast<AnnotationCtx*>(data);
        const CXCursorKind kind = clang_getCursorKind(cursor);

        if (kind == CXCursor_MacroExpansion)
        {
            CXString sp = clang_getCursorSpelling(cursor);
            const char* cname = clang_getCString(sp);
            const std::string name = cname ? cname : "";
            clang_disposeString(sp);
            if ((name == "CA_REFLECT" || name == "CA_CODEGEN_REGISTER" || name == "CA_REFLECTION") &&
                cursorIsInMainFile(cursor, ctx->filePath))
            {
                CXSourceRange r = clang_getCursorExtent(cursor);
                unsigned a = 0, b = 0, line = 0, column = 0;
                CXFile fa = nullptr, fb = nullptr;
                clang_getFileLocation(clang_getRangeStart(r), &fa, &line, &column, &a);
                clang_getFileLocation(clang_getRangeEnd(r), &fb, &line, &column, &b);
                ctx->uses->push_back({ name, std::string(), a, b });
            }
            return CXChildVisit_Continue;
        }

        if (kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl ||
            kind == CXCursor_ClassTemplate || kind == CXCursor_ClassTemplatePartialSpecialization)
        {
            // Only file-scope and namespace-scope types: a struct declared inside a function or nested in
            // another class is not a delivery target (task 9.57). Class templates are recorded as well, so
            // that a file which annotates ONLY a template is not reported as "no annotated types";
            // extracting one still fails later, because the target search knows only struct/class cursors.
            const CXCursorKind parentKind = clang_getCursorKind(clang_getCursorSemanticParent(cursor));
            if ((parentKind == CXCursor_TranslationUnit || parentKind == CXCursor_Namespace) &&
                cursorIsInMainFile(cursor, ctx->filePath))
            {
                CXString sp = clang_getCursorSpelling(cursor);
                const char* cname = clang_getCString(sp);
                const std::string name = cname ? cname : "";
                clang_disposeString(sp);
                if (!name.empty())
                {
                    unsigned line = 0, column = 0, offset = 0;
                    CXFile file = nullptr;
                    clang_getFileLocation(clang_getCursorLocation(cursor), &file, &line, &column, &offset);
                    ctx->types->push_back(
                        { offset, name, cursor, clang_isCursorDefinition(cursor) != 0 });
                }
            }
            return CXChildVisit_Continue; // do not descend into nested declarations
        }
        return CXChildVisit_Recurse;
    }

    /** The invocation text of a real macro expansion: the bytes between its first '(' and the matching
        ')'. Reading these bytes is safe precisely because clang told us an invocation is there. */
    std::string invocationArgs(const std::string& source, const AnnotationUse& use)
    {
        if (use.begin >= source.size() || use.end > source.size() || use.end <= use.begin)
            return {};
        const std::size_t open = source.find('(', use.begin);
        if (open == std::string::npos || open >= use.end)
            return {};
        int depth = 0;
        for (std::size_t i = open; i < use.end; ++i)
        {
            if (source[i] == '(') ++depth;
            else if (source[i] == ')')
            {
                if (--depth == 0)
                    return trim(source.substr(open + 1, i - open - 1));
            }
        }
        return {};
    }

    struct AnnotationBinding
    {
        std::string macro;
        std::string args;
        CXCursor    cursor{};
        bool        isDefinition = false;
    };

    /** A type is annotated when a real annotation invocation is followed by its declaration.

        Two of the three channels carry the annotated type's NAME instead of sitting next to its
        definition, so they resolve by name: `CA_REFLECTION(Type, a, b)` names the type as its first
        argument (measured: Tools/CodeGen/TestExternal.h declares the member list and no struct at all),
        and `CA_CODEGEN_REGISTER("Qualified.Name")` names it inside the string literal. Only `CA_REFLECT`
        binds to the type declaration that follows the invocation. */
    std::map<std::string, AnnotationBinding> bindAnnotations(const std::vector<AnnotationUse>& uses,
                                                             const std::vector<TypeOccurrence>& types)
    {
        std::map<std::string, AnnotationBinding> bound;
        for (const AnnotationUse& u : uses)
        {
            std::string named;
            if (u.macro == "CA_REFLECTION")
            {
                const std::size_t comma = u.args.find(',');
                named = trim(comma == std::string::npos ? u.args : u.args.substr(0, comma));
            }
            else if (u.macro == "CA_CODEGEN_REGISTER")
            {
                const std::size_t q1 = u.args.find('"');
                const std::size_t q2 = (q1 == std::string::npos) ? std::string::npos : u.args.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos)
                    named = u.args.substr(q1 + 1, q2 - q1 - 1);
            }
            if (!named.empty())
            {
                bound[named] = { u.macro, u.args, CXCursor{}, false };
                continue;
            }
            for (const TypeOccurrence& t : types)
            {
                if (t.offset > u.end)
                {
                    bound[t.name] = { u.macro, u.args, t.cursor, t.isDefinition };
                    break;
                }
            }
        }
        return bound;
    }

    std::string scanMarkerOptions(const std::map<std::string, AnnotationBinding>& bound,
                                  const std::string& typeName)
    {
        const auto it = bound.find(typeName);
        return (it == bound.end() || it->second.macro != "CA_REFLECT") ? std::string() : it->second.args;
    }

    /** Task 6b.3 + 9.59: the CA_REFLECTION(Type, a, b) member list, taken from the invocation the
        preprocessor reported (not from a text search). The first argument names the type; the rest are the
        member names in declaration order. */
    std::vector<std::string> scanCA_REFLECTIONList(const std::map<std::string, AnnotationBinding>& bound,
                                                   const std::string& typeName)
    {
        for (const auto& kv : bound)
        {
            if (kv.second.macro != "CA_REFLECTION")
                continue;
            std::vector<std::string> parts;
            std::string cur;
            for (char c : kv.second.args)
            {
                if (c == ',') { parts.push_back(trim(cur)); cur.clear(); }
                else cur += c;
            }
            parts.push_back(trim(cur));
            if (!parts.empty() && unqualifiedName(parts[0]) == unqualifiedName(typeName))
            {
                std::vector<std::string> members;
                for (std::size_t i = 1; i < parts.size(); ++i)
                    if (!parts[i].empty())
                        members.push_back(parts[i]);
                return members; // the first matching declaration wins
            }
        }
        return {};
    }

    // --- Task 5.1: reflection emitter. The tool does NOT link CodeGenMeta.h (it is a pure
    // libclang text producer, not part of the engine build), so it emits *enum-NAME strings*
    // (e.g. "TypeKind::Int") and relies on the consumer TU to resolve them against cacore::codegen.
    struct EmitField
    {
        std::string name;     // member name, e.g. "a"
        std::string typeName; // spelled C++ type, e.g. "float"; empty => emit nullptr (unknown type)
        std::string kindName; // semantic TypeKind::X enum-name string
        // Task 2.2: container facts copied from the analysis, emitted as flags + element/key tables.
        bool        isContainer = false;
        bool        unordered = false;
        bool        bitPacked = false;
        bool        hasOpaqueElement = false;
        bool        wrapper = false;
        std::string element2TypeName;
        std::string element2KindName;
        bool        element2IsContainer = false;
        bool        isWrapper = false;
        std::string keyTypeName;
        std::string keyKindName;
        std::string elementTypeName;
        std::string elementKindName;
        bool        elementIsContainer = false;
    };

    std::string reflectionFlags(const EmitField& f)
    {
        std::string flags = "FieldFlags::HashInclude";
        if (f.isContainer)          flags += " | FieldFlags::Container";
        if (f.unordered)            flags += " | FieldFlags::Unordered";
        if (f.hasOpaqueElement)     flags += " | FieldFlags::HasOpaqueElement";
        if (f.wrapper)              flags += " | FieldFlags::Wrapper";
        if (f.isWrapper)            flags += " | FieldFlags::Wrapper";
        return flags;
    }

    // Map a libclang CXTypeKind to the semantic cacore::codegen::TypeKind enum NAME.
    const char* fieldTypeKindEnum(CXTypeKind k)
    {
        switch (k)
        {
            case CXType_Bool:                 return "TypeKind::Bool";
            case CXType_Char_S:
            case CXType_SChar:
            case CXType_Short:
            case CXType_Int:
            case CXType_Long:
            case CXType_LongLong:
            case CXType_Int128:               return "TypeKind::Int";
            case CXType_Char_U:
            case CXType_UChar:
            case CXType_UShort:
            case CXType_UInt:
            case CXType_ULong:
            case CXType_ULongLong:
            case CXType_UInt128:              return "TypeKind::UInt";
            case CXType_Float:                return "TypeKind::Float";
            case CXType_Double:               return "TypeKind::Double";
            case CXType_Enum:                 return "TypeKind::Enum";
            case CXType_Record:               return "TypeKind::Struct";
            case CXType_Pointer:              return "TypeKind::Opaque";
            case CXType_ConstantArray:
            case CXType_IncompleteArray:      return "TypeKind::Array";
            default:                          return "TypeKind::Opaque";
        }
    }

    // Derive the fully-qualified type name from the AST (walk semantic parents up to the translation
    // unit). This is why the CA_REFLECT marker needs NO type name — the tool gets the qualified name
    // from where the type actually lives. E.g. codegen_test::CodeGenSample / global CodeGenExternal.
    std::string buildQualifiedName(CXCursor c)
    {
        std::vector<std::string> parts;
        CXString s = clang_getCursorSpelling(c);
        parts.push_back(clang_getCString(s));
        clang_disposeString(s);

        CXCursor p = clang_getCursorSemanticParent(c);
        for (int i = 0; i < 32; ++i)
        {
            CXCursorKind kind = clang_getCursorKind(p);
            if (kind == CXCursor_Namespace || kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl)
            {
                CXString ns = clang_getCursorSpelling(p);
                parts.push_back(clang_getCString(ns));
                clang_disposeString(ns);
                p = clang_getCursorSemanticParent(p);
            }
            else
            {
                break;
            }
        }

        std::string qname;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it)
        {
            if (!qname.empty())
                qname += "::";
            qname += *it;
        }
        return qname;
    }

    // Emit a readable, constexpr TypeDescriptor<T> specialization (the shape the ABI guard / runtime
    // readers consume). Deterministic: the field list arrives in DECLARATION order and is emitted as-is
    // (no sort — see task 9.5a), so the same input always yields the same bytes.
    void emitTypeDescriptor(const std::vector<EmitField>& fields, const std::string& qname, FILE* out)
    {
        std::fprintf(out,
            "// Generated by CodeGen reflection emitter for %s.\n"
            "// Requires: the reflected type is fully defined (for offsetof) and CodeGenMeta.h is on\n"
            "// the include path, before this fragment. Deterministic: same input => same bytes.\n"
            "#pragma once\n"
            "\n",
            qname.c_str());
        std::fprintf(out, "namespace cacore::codegen\n{\n");
        std::fprintf(out, "template<>\n");
        std::fprintf(out, "struct TypeDescriptor<%s>\n{\n", qname.c_str());
        std::fprintf(out, "    static constexpr const char*              name       = \"%s\";\n", qname.c_str());
        std::fprintf(out, "    static constexpr std::size_t              fieldCount = %zu;\n", fields.size());
        if (fields.empty())
        {
            std::fprintf(out, "    static constexpr const CodeGenFieldInfo* fields() { return nullptr; }\n");
        }
        else
        {
            // Task 2.2: one-entry descriptor tables for container members. `offset` is 0 because an
            // element is not a member of the parent type; `name` is the role ("key" / "value").
            for (const EmitField& f : fields)
            {
                if (!f.isContainer)
                    continue;
                std::string elemFlags = "FieldFlags::HashInclude";
                if (f.elementIsContainer) elemFlags += " | FieldFlags::Container";
                if (!f.keyTypeName.empty())
                    std::fprintf(out,
                        "    static constexpr CodeGenFieldInfo key_%s[1] = {\n"
                        "        { \"key\", \"%s\", 0, %s, FieldFlags::HashInclude },\n    };\n",
                        f.name.c_str(), f.keyTypeName.c_str(), f.keyKindName.c_str());
                const bool elemIsArray = (f.elementKindName == "TypeKind::Array");
                std::string elem2Flags = "FieldFlags::HashInclude";
                if (f.element2IsContainer) elem2Flags += " | FieldFlags::Container";
                if (!f.element2TypeName.empty())
                    std::fprintf(out,
                        "    static constexpr CodeGenFieldInfo elem2_%s[1] = {\n"
                        "        { \"value\", \"%s\", 0, %s, %s },\n    };\n",
                        f.name.c_str(), f.element2TypeName.c_str(), f.element2KindName.c_str(),
                        elem2Flags.c_str());
                if (!f.element2TypeName.empty())
                    std::fprintf(out,
                        "    static constexpr CodeGenFieldInfo elem_%s[1] = {\n"
                        "        { \"value\", \"%s\", 0, %s, %s, nullptr, elem2_%s },\n    };\n",
                        f.name.c_str(), f.elementTypeName.c_str(), f.elementKindName.c_str(),
                        elemFlags.c_str(), f.name.c_str());
                else
                    std::fprintf(out,
                        "    static constexpr CodeGenFieldInfo elem_%s[1] = {\n"
                        "        { \"value\", \"%s\", 0, %s, %s },\n    };\n",
                        f.name.c_str(), f.elementTypeName.c_str(), f.elementKindName.c_str(),
                        elemFlags.c_str());
            }
            std::fprintf(out, "\n    static constexpr const CodeGenFieldInfo* fields() { return arr; }\n");
            std::fprintf(out, "    static constexpr CodeGenFieldInfo arr[%zu] = {\n", fields.size());
            for (const EmitField& f : fields)
            {
                const std::string flags = reflectionFlags(f);
                const std::string keyPtr = f.keyTypeName.empty() ? "nullptr" : ("key_" + f.name);
                const std::string elemPtr = f.isContainer ? ("elem_" + f.name) : "nullptr";
                if (f.typeName.empty())
                    std::fprintf(out,
                        "        { \"%s\", nullptr, offsetof(%s, %s), %s, %s, %s, %s },\n",
                        f.name.c_str(), qname.c_str(), f.name.c_str(), f.kindName.c_str(),
                        flags.c_str(), keyPtr.c_str(), elemPtr.c_str());
                else
                    std::fprintf(out,
                        "        { \"%s\", \"%s\", offsetof(%s, %s), %s, %s, %s, %s },\n",
                        f.name.c_str(), f.typeName.c_str(), qname.c_str(), f.name.c_str(),
                        f.kindName.c_str(), flags.c_str(), keyPtr.c_str(), elemPtr.c_str());
            }
            std::fprintf(out, "    };\n");
        }
        std::fprintf(out, "};\n");
        std::fprintf(out, "} // namespace cacore::codegen\n");
    }

    // --- Task 6.1: serialization emitter. Generates a `CodeGenSerializer<T>` that (de)serializes the
    // reflected type field-by-field in DECLARATION ORDER (the order the type table carries,
    // which matches CA_REFLECTION's member order). It is a PARALLEL, data-driven exit distinct from the
    // runtime `careflection`/`visit_members` path (which Serialization.h keeps using). Gated on the
    // `serializable` marker option (design AUDIT-4, decided 2026-09-08): only emitted for serializable
    // types, so `serializable` is actually consumed here. Fragments, like the reflection emitter — the
    // consumer TU must have CodeGenMeta.h + Serialization.h on the include path, and the type defined.
    void emitCodeGenSerializer(const std::vector<EmitField>& fields, const std::string& qname, FILE* out)
    {
        const bool anyUnordered = std::any_of(fields.begin(), fields.end(), [](const EmitField& f) {
            return f.isContainer && f.unordered && !f.bitPacked;
        });
        const bool anyContainer = std::any_of(fields.begin(), fields.end(),
                                              [](const EmitField& f) { return f.isContainer; });
        std::fprintf(out,
            "// Generated by CodeGen serialization emitter for %s.\n"
            "// Requires: the type is defined + CodeGenMeta.h + Serialization.h on the include path.\n"
            "// Deterministic: declaration-order field traversal, same input => same bytes.\n"
            "// Unordered containers are flattened and sorted before writing (task 3.1), and every\n"
            "// container is cleared before reading (task 3.4), so the bytes and the read-back result\n"
            "// depend on the CONTENT only — never on the insertion history of the container.\n"
            "#pragma once\n"
            "\n"
            "#include \"CodeGenMeta.h\"\n"
            "#include \"Serialization.h\"\n"
            "%s%s%s"
            "\n",
            qname.c_str(),
            anyUnordered ? "#include <algorithm>   // std::sort for the canonical container order\n" : "",
            anyUnordered ? "#include <utility>     // std::pair for the flattened map entries\n" : "",
            (anyUnordered || anyContainer) ? "#include <type_traits> // the container traits below\n"
                                             "#include <vector>      // the flattened array itself\n"
                                           : "");
        std::fprintf(out, "namespace cacore::codegen\n{\n");
        if (anyContainer)
        {
            // Container IO is STRUCTURAL recursion over the container type itself, so it covers
            // arbitrary nesting (`vector<vector<int>>`, `map<K, set<V>>`, `T m[2][3]`) with no
            // per-type metadata. The old runtime cannot do this: its read path has no branch for
            // "element is itself a container" and asserts (measured: Serialization.h static
            // assertion 'Wrong'), which is why container IO belongs to the generated code.
            std::fprintf(out,
                "template<typename T, typename = void> struct CGIsUnordered : std::false_type {};\n"
                "template<typename T> struct CGIsUnordered<T, std::void_t<decltype(std::declval<T const&>().bucket_count())>> : std::true_type {};\n"
                "template<typename T, typename = void> struct CGIsMapLike : std::false_type {};\n"
                "template<typename T> struct CGIsMapLike<T, std::void_t<typename T::mapped_type>> : std::true_type {};\n"
                "template<typename T> struct CGIsContainer\n"
                "    : std::integral_constant<bool, careflection::containerStates<T>::is_container_with_size> {};\n"
                "// map/set expose pair<const K, V>: the flattened array needs an assignable element\n"
                "template<typename T, typename = void> struct CGFlat { using type = T; };\n"
                "template<typename T> struct CGFlat<T, std::void_t<typename T::first_type, typename T::second_type>>\n"
                "{\n"
                "    using type = std::pair<std::remove_const_t<typename T::first_type>, typename T::second_type>;\n"
                "};\n"
                "\n"
                "template<typename ByteBuffer, typename E> void cgWriteValue(::cacore::serializer<ByteBuffer>& s, E const& e);\n"
                "template<typename ByteBuffer, typename E> void cgReadValue(::cacore::deserializer<ByteBuffer>& d, E& e);\n"
                "template<typename ByteBuffer, typename C> void cgWriteContainer(::cacore::serializer<ByteBuffer>& s, C const& c);\n"
                "template<typename ByteBuffer, typename C> void cgReadContainer(::cacore::deserializer<ByteBuffer>& d, C& c);\n"
                "\n"
                "// Writes the element count, then every element - sorted first when the container's traversal\n"
                "// order is unspecified, so the bytes depend on the CONTENT only.\n"
                "template<typename ByteBuffer, typename C>\n"
                "void cgWriteContainerSized(::cacore::serializer<ByteBuffer>& s, C const& c)\n"
                "{\n"
                "    using E0 = typename C::value_type;\n"
                "    using E = typename CGFlat<E0>::type;  // map value_type carries a const key\n"
                "    if constexpr (CGIsUnordered<C>::value)\n"
                "    {\n"
                "        std::vector<E> flat(c.begin(), c.end());\n"
                "        std::sort(flat.begin(), flat.end(), [](E const& a, E const& b) {\n"
                "            if constexpr (CGIsMapLike<C>::value) return a.first < b.first;\n"
                "            else return a < b; });\n"
                "        s.serialize(static_cast<unsigned long long>(flat.size()));\n"
                "        for (auto const& e : flat) cgWriteValue(s, e);\n"
                "    }\n"
                "    else\n"
                "    {\n"
                "        s.serialize(static_cast<unsigned long long>(c.size()));\n"
                "        for (auto const& e : c) cgWriteValue(s, e);\n"
                "    }\n"
                "}\n"
                "\n"
                "// C arrays (including every extra dimension) are written as (extent, elements), which is exactly\n"
                "// the multi-dimension shape the disk contract describes.\n"
                "template<typename ByteBuffer, typename C>\n"
                "void cgWriteContainer(::cacore::serializer<ByteBuffer>& s, C const& c)\n"
                "{\n"
                "    if constexpr (std::is_array_v<C>)\n"
                "    {\n"
                "        s.serialize(static_cast<unsigned long long>(std::extent_v<C>));\n"
                "        for (auto const& e : c) cgWriteValue(s, e);\n"
                "    }\n"
                "    else cgWriteContainerSized(s, c);\n"
                "}\n"
                "\n"
                "template<typename ByteBuffer, typename E>\n"
                "void cgWriteValue(::cacore::serializer<ByteBuffer>& s, E const& e)\n"
                "{\n"
                "    if constexpr (std::is_array_v<E>) cgWriteContainer(s, e);\n"
                "    else if constexpr (CGIsContainer<E>::value) cgWriteContainer(s, e);\n"
                "    else if constexpr (CGIsMapLike<std::remove_cv_t<E>>::value == false && requires { e.first; e.second; })\n"
                "    { cgWriteValue(s, e.first); cgWriteValue(s, e.second); }\n"
                "    else s.serialize(e);\n"
                "}\n"
                "\n"
                "// Clears the target first, then rebuilds it from the payload alone: the result must depend on the\n"
                "// payload only, never on whatever the target happened to hold before.\n"
                "template<typename ByteBuffer, typename C>\n"
                "void cgReadContainerSized(::cacore::deserializer<ByteBuffer>& d, C& c)\n"
                "{\n"
                "    c.clear();\n"
                "    unsigned long long n = 0;\n"
                "    d.deserialize(n);\n"
                "    for (unsigned long long i = 0; i < n; ++i)\n"
                "    {\n"
                "        using E = typename C::value_type;\n"
                "        if constexpr (CGIsMapLike<C>::value)\n"
                "        {\n"
                "            using K = std::remove_const_t<typename E::first_type>;\n"
                "            using V = typename E::second_type;\n"
                "            K k{}; V v{}; cgReadValue(d, k); cgReadValue(d, v);\n"
                "            c.insert(std::make_pair(std::move(k), std::move(v)));\n"
                "        }\n"
                "        else\n"
                "        {\n"
                "            E e{}; cgReadValue(d, e); c.insert(c.end(), std::move(e));\n"
                "        }\n"
                "    }\n"
                "}\n"
                "\n"
                "template<typename ByteBuffer, typename C>\n"
                "void cgReadContainer(::cacore::deserializer<ByteBuffer>& d, C& c)\n"
                "{\n"
                "    if constexpr (std::is_array_v<C>)\n"
                "    {\n"
                "        unsigned long long n = 0;\n"
                "        d.deserialize(n);\n"
                "        for (unsigned long long i = 0; i < n && i < std::extent_v<C>; ++i) cgReadValue(d, c[i]);\n"
                "    }\n"
                "    else cgReadContainerSized(d, c);\n"
                "}\n"
                "\n"
                "template<typename ByteBuffer, typename E>\n"
                "void cgReadValue(::cacore::deserializer<ByteBuffer>& d, E& e)\n"
                "{\n"
                "    if constexpr (std::is_array_v<E>) cgReadContainer(d, e);\n"
                "    else if constexpr (CGIsContainer<E>::value) cgReadContainer(d, e);\n"
                "    else if constexpr (requires { e.first; e.second; })\n"
                "    { cgReadValue(d, e.first); cgReadValue(d, e.second); }\n"
                "    else d.deserialize(e);\n"
                "}\n"
                "\n");
        }
        std::fprintf(out, "template<typename T> struct CodeGenSerializer;  // primary template\n\n");
        std::fprintf(out, "template<>\nstruct CodeGenSerializer<%s>\n{\n", qname.c_str());
        std::fprintf(out, "    using Obj = %s;\n", qname.c_str());
        std::fprintf(out, "    template<typename ByteBuffer>\n    static void serialize(Obj const& obj, ByteBuffer& buffer)\n    {\n");
        std::fprintf(out, "        ::cacore::serializer<ByteBuffer> s(buffer);  // one instance carries offset\n");
        for (const EmitField& f : fields)
        {
            // Task 3.1 / 3.2: every container (ordered or not, nested or not) is written by the
            // emitted structural writer, which sorts unordered containers at every level.
            if (f.isContainer && !f.bitPacked)
                std::fprintf(out, "        cgWriteContainer(s, obj.%s);\n", f.name.c_str());
            else
                std::fprintf(out, "        s.serialize(obj.%s);\n", f.name.c_str());
        }
        std::fprintf(out,
            "    }\n"
            "    template<typename ByteBuffer>\n"
            "    static void deserialize(Obj& obj, ByteBuffer const& buffer)\n"
            "    {\n"
            "        ::cacore::deserializer<ByteBuffer> d(buffer);  // one instance carries offset\n");
        for (const EmitField& f : fields)
        {
            // Task 3.4 / 3.2: the structural reader clears the target first and rebuilds it from the
            // payload alone — and it recurses, which the old runtime's reader cannot do.
            if (f.isContainer && !f.bitPacked)
                std::fprintf(out, "        cgReadContainer(d, obj.%s);\n", f.name.c_str());
            else
                std::fprintf(out, "        d.deserialize(obj.%s);\n", f.name.c_str());
        }
        std::fprintf(out,
            "    }\n"
            "};\n");
        std::fprintf(out, "} // namespace cacore::codegen\n");
    }

    // --- Task 6.2: hash + equality emitter. Generates a `custom_hash_trait<T>` (the hook the existing
    // `aggregateHasher` already calls via `has_custom_hash_func`) plus a field-wise `equal`. It hashes
    // EACH FIELD in declaration order, NOT the whole struct raw bytes (=> padding/layout independent),
    // and normalizes floats (map -0.0 -> +0.0 and every NaN -> a canonical NaN) so hash(-0.0)==hash(+0.0)
    // and NaN hashes stably. `operator==`-equivalent semantics: float fields compare `==` OR both-NaN.
    // Non-goal: does NOT migrate the existing HashObj semantic (per proposal), it's a generated trait.
    void emitHashTrait(const std::vector<EmitField>& fields, const std::string& qname, FILE* out)
    {
        std::fprintf(out,
            "// Generated by CodeGen hash/equality emitter for %s.\n"
            "// Field-wise (padding/layout independent), floats normalized (-0.0=+0.0, NaN canonical).\n"
            "// Requires: type defined + Hasher.h on the include path. Deterministic (declaration order).\n"
            "#pragma once\n"
            "\n"
            "#include \"Hasher.h\"\n"
            "#include <limits>\n"
            "#include <algorithm>\n"
            "#include <iterator>\n"
            "#include <type_traits>\n"
            "#include <utility>\n"
            "#include <vector>\n"
            "\n",
            qname.c_str());
        const bool anyContainer = std::any_of(fields.begin(), fields.end(),
            [](const EmitField& f) { return f.isContainer; });
        if (anyContainer)
        {
            // Task 3.3 / 3.2: a container's hash must not depend on its traversal order, and each
            // element must be hashed by ITS OWN specialization -- `h.hash(element)` takes the
            // trivially copyable shortcut and never reaches the element's trait (measured; see the
            // design record), and floats inside a container bypass the -0.0 / NaN normalization.
            std::fprintf(out,
                "template<typename T, typename = void> struct CGHashIsUnordered : std::false_type {};\n"
                "template<typename T> struct CGHashIsUnordered<T, std::void_t<decltype(std::declval<T const&>().bucket_count())>> : std::true_type {};\n"
                "template<typename T, typename = void> struct CGHashIsMapLike : std::false_type {};\n"
                "template<typename T> struct CGHashIsMapLike<T, std::void_t<typename T::mapped_type>> : std::true_type {};\n"
                "template<typename T> struct CGHashIsContainer\n"
                "    : std::integral_constant<bool, careflection::containerStates<T>::is_container_with_size> {};\n"
                "template<typename T, typename = void> struct CGHashFlat { using type = T; };\n"
                "template<typename T> struct CGHashFlat<T, std::void_t<typename T::first_type, typename T::second_type>>\n"
                "{\n"
                "    using type = std::pair<std::remove_const_t<typename T::first_type>, typename T::second_type>;\n"
                "};\n"
                "\n"
                "// Container equality: `std::equal` would compare the ROWS of a multi-dimensional C\n"
                "// array by address (array-to-pointer decay), so arrays recurse element-wise.\n"
                "template<typename C> bool cgEqualContainer(C const& a, C const& b)\n"
                "{\n"
                "    if constexpr (std::is_array_v<C>)\n"
                "    {\n"
                "        for (std::size_t i = 0; i < std::extent_v<C>; ++i)\n"
                "            if (!cgEqualContainer(a[i], b[i])) return false;\n"
                "        return true;\n"
                "    }\n"
                "    else return a == b;\n"
                "}\n"
                "template<typename H, typename C> void cgHashContainer(H& h, C const& c);\n"
                "template<typename H, typename E> void cgHashContainerValue(H& h, E const& e)\n"
                "{\n"
                "    using V = std::remove_cvref_t<E>;\n"
                "    if constexpr (std::is_same_v<V, float>)\n"
                "    { float v = e; if (v == 0.0f) v = 0.0f; else if (v != v) v = std::numeric_limits<float>::quiet_NaN(); h.hash_raw(&v, sizeof(v)); }\n"
                "    else if constexpr (std::is_same_v<V, double>)\n"
                "    { double v = e; if (v == 0.0) v = 0.0; else if (v != v) v = std::numeric_limits<double>::quiet_NaN(); h.hash_raw(&v, sizeof(v)); }\n"
                "    else if constexpr (std::is_array_v<V>) cgHashContainer(h, e);\n"
                "    else if constexpr (CGHashIsContainer<V>::value) cgHashContainer(h, e);\n"
                "    else if constexpr (cacore::has_custom_hash_func<V, H>) cacore::custom_hash_trait<V>::hash(e, h);\n"
                "    else h.hash(e);\n"
                "}\n"
                "\n"
                "template<typename H, typename C>\n"
                "void cgHashContainerSized(H& h, C const& c)\n"
                "{\n"
                "    using E0 = typename C::value_type;\n"
                "    using E = typename CGHashFlat<E0>::type;\n"
                "    if constexpr (CGHashIsUnordered<C>::value)\n"
                "    {\n"
                "        std::vector<E> flat(c.begin(), c.end());\n"
                "        std::sort(flat.begin(), flat.end(), [](E const& a, E const& b) {\n"
                "            if constexpr (CGHashIsMapLike<C>::value) return a.first < b.first;\n"
                "            else return a < b; });\n"
                "        h.hash(static_cast<unsigned long long>(flat.size()));\n"
                "        for (auto const& e : flat)\n"
                "        {\n"
                "            if constexpr (CGHashIsMapLike<C>::value) { cgHashContainerValue(h, e.first); cgHashContainerValue(h, e.second); }\n"
                "            else cgHashContainerValue(h, e);\n"
                "        }\n"
                "    }\n"
                "    else\n"
                "    {\n"
                "        h.hash(static_cast<unsigned long long>(c.size()));\n"
                "        for (auto const& e : c)\n"
                "        {\n"
                "            if constexpr (CGHashIsMapLike<C>::value) { cgHashContainerValue(h, e.first); cgHashContainerValue(h, e.second); }\n"
                "            else cgHashContainerValue(h, e);\n"
                "        }\n"
                "    }\n"
                "}\n"
                "\n"
                "template<typename H, typename C>\n"
                "void cgHashContainer(H& h, C const& c)\n"
                "{\n"
                "    if constexpr (std::is_array_v<C>)\n"
                "    {\n"
                "        h.hash(static_cast<unsigned long long>(std::extent_v<C>));\n"
                "        for (auto const& e : c) cgHashContainerValue(h, e);\n"
                "    }\n"
                "    else cgHashContainerSized(h, c);\n"
                "}\n"
                "\n");
        }
        std::fprintf(out, "namespace cacore\n{\n");
        std::fprintf(out, "template<typename T> struct custom_hash_trait;\n"); // ensure primary is declared
        std::fprintf(out, "template<>\n");
        std::fprintf(out, "struct custom_hash_trait<%s>\n{\n", qname.c_str());
        std::fprintf(out, "    using Obj = %s;\n", qname.c_str());
        std::fprintf(out,
            "    template<typename hasher_t>\n"
            "    static void hash(Obj const& obj, hasher_t& h)\n"
            "    {\n");
        for (const EmitField& f : fields)
        {
            if (f.isContainer)
            {
                std::fprintf(out, "        cgHashContainer(h, obj.%s);\n", f.name.c_str());
            }
            else if (f.kindName == "TypeKind::Float")
            {
                std::fprintf(out,
                    "        { float v = obj.%s; if (v == 0.0f) v = 0.0f; else if (v != v) v = std::numeric_limits<float>::quiet_NaN(); h.hash_raw(&v, sizeof(v)); }\n",
                    f.name.c_str());
            }
            else if (f.kindName == "TypeKind::Double")
            {
                std::fprintf(out,
                    "        { double v = obj.%s; if (v == 0.0) v = 0.0; else if (v != v) v = std::numeric_limits<double>::quiet_NaN(); h.hash_raw(&v, sizeof(v)); }\n",
                    f.name.c_str());
            }
            else
            {
                std::fprintf(out, "        h.hash(obj.%s);\n", f.name.c_str());
            }
        }
        std::fprintf(out,
            "    }\n"
            "    static bool equal(Obj const& a, Obj const& b)\n"
            "    {\n");
        for (const EmitField& f : fields)
        {
            if (f.kindName == "TypeKind::Float" || f.kindName == "TypeKind::Double")
            {
                // NaN-tolerant: equal if fields are == OR both are NaN (a.x != a.x is the NaN test;
                // this also makes -0.0 == +0.0 because == returns true for those).
                std::fprintf(out,
                    "        if (!(a.%s == b.%s || (a.%s != a.%s && b.%s != b.%s))) return false;\n",
                    f.name.c_str(), f.name.c_str(),
                    f.name.c_str(), f.name.c_str(),
                    f.name.c_str(), f.name.c_str());
            }
            else if (f.isContainer)
            {
                // Task 3.2: `a.arr != b.arr` on a C array compares ADDRESSES (array-to-pointer decay),
                // so container members are compared element-wise over both full ranges.
                std::fprintf(out,
                    "        if (!cgEqualContainer(a.%s, b.%s)) return false;\n",
                    f.name.c_str(), f.name.c_str());
            }
            else
            {
                std::fprintf(out, "        if (a.%s != b.%s) return false;\n", f.name.c_str(), f.name.c_str());
            }
        }
        std::fprintf(out, "        return true;\n    }\n};\n");
        std::fprintf(out, "} // namespace cacore\n");
    }

    // --- Task 3.3: whitelist gating (design D2 "标过了才生成").
    // The tool must NOT reflect every struct it parses — only types that carry a discovery marker.
    // We scan the SOURCE TEXT (not libclang attributes, which libclang's C API reads weakly) for the
    // three marker forms and build the whitelist of "cross-the-boundary" type names:
    //   (a) CA_CODEGEN_REGISTER("QualifiedName")   -- string-literal arg (so :: is fine)
    //   (b) CA_REFLECT(...)                         -- type is the class/struct body FOLLOWING the marker
    //   (c) CA_REFLECTION(Type, a, b)               -- first comma-arg is the type name
    // Empty whitelist => nothing is annotated => EXIT 0 + no file (the spec's "空名单不阻断 build"
    // config). A non-empty whitelist whose CLI target is NOT in it => the type was not marked => refuse
    // to extract (the negative gate). Deterministic: pure text scan, no cursor/metadata.
    std::string unqualifiedName(const std::string& s)
    {
        std::size_t c = s.rfind("::");
        return c == std::string::npos ? s : s.substr(c + 2);
    }

    bool isWhitelisted(const std::string& target, const std::vector<std::string>& whitelist)
    {
        const std::string t = unqualifiedName(target);
        for (const std::string& q : whitelist)
        {
            if (q == target)                return true; // exact match
            if (unqualifiedName(q) == t)    return true; // tail match (either side qualified)
        }
        return false;
    }

} // namespace

// Task 1.2: the diagnostic scan, factored out so it can also run BEFORE the "no annotated types" early
// return. Measured defect it fixes: a syntax error, an unresolvable `#include`, and a marker macro that
// was never defined all produced ZERO annotations, and the tool then printed "no annotated types (empty
// whitelist)" and exited 0 — so a broken parse was indistinguishable from a clean header that has
// nothing to generate, and a wrong compile environment looked like success.
struct DiagnosticScan
{
    std::string fatalMsg;      // first fatal diagnostic (unresolved include and friends)
    unsigned    inHeader = 0;  // error-level diagnostics inside the file we were asked to parse

    bool untrustworthy() const { return !fatalMsg.empty() || inHeader > 0; }
};

static DiagnosticScan scanDiagnostics(CXTranslationUnit tu, const std::string& filePath)
{
    DiagnosticScan scan;
    const std::string want = baseName(filePath);
    const unsigned    n    = clang_getNumDiagnostics(tu);
    for (unsigned i = 0; i < n; ++i)
    {
        CXDiagnostic d   = clang_getDiagnostic(tu, i);
        const int    sev = static_cast<int>(clang_getDiagnosticSeverity(d));
        if (sev >= CXDiagnostic_Fatal && scan.fatalMsg.empty())
        {
            CXString msg = clang_formatDiagnostic(d, clang_defaultDiagnosticDisplayOptions());
            const char* c = clang_getCString(msg);
            scan.fatalMsg = (c != nullptr) ? c : "(unformattable)";
            clang_disposeString(msg);
        }
        if (sev >= CXDiagnostic_Error)
        {
            CXSourceLocation loc = clang_getDiagnosticLocation(d);
            CXFile file = nullptr;
            unsigned line = 0, col = 0, offset = 0;
            clang_getExpansionLocation(loc, &file, &line, &col, &offset);
            if (file != nullptr)
            {
                CXString fn = clang_getFileName(file);
                const char* cstr = clang_getCString(fn);
                const bool same = (cstr != nullptr) && (baseName(cstr) == want);
                clang_disposeString(fn);
                if (same) ++scan.inHeader;
            }
        }
        clang_disposeDiagnostic(d);
    }
    return scan;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr,
            "usage: CodeGen <file.h> <TypeName> [--emit-reflection] [--qualified-name=<qn>] [-o <out>] [clang-arg ...]\n");
        return 2;
    }

    const char* filePath = argv[1];
    const char* typeName = argv[2];

    // Task 5.1/6.1: codegen flags (pulled out of the clang-arg list so they are never fed to libclang).
    bool        emitReflection    = false;
    bool        emitSerialization = false;
    bool        emitHash          = false;
    const char* outPath           = nullptr;
    std::string qualifiedNameOverride;
    std::vector<const char*> clangArgs;
    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--emit-reflection")
        {
            emitReflection = true;
        }
        else if (a == "--emit-serialization")
        {
            emitSerialization = true;
        }
        else if (a == "--emit-hash")
        {
            emitHash = true;
        }
        else if (a == "-o")
        {
            if (i + 1 < argc)
                outPath = argv[++i];
        }
        else if (a == "--qualified-name")
        {
            if (i + 1 < argc)
                qualifiedNameOverride = argv[++i];
        }
        else if (a.rfind("--qualified-name=", 0) == 0)
        {
            qualifiedNameOverride = a.substr(17);
        }
        else
        {
            clangArgs.push_back(argv[i]);
        }
    }

    // Task 3.3 whitelist gating (design D2 "标过了才生成"): only emit a type that carries a discovery
    // marker (CA_REFLECT / CA_CODEGEN_REGISTER / CA_REFLECTION). Empty whitelist => EXIT 0 + no file
    // (spec "空名单不阻断 build"); non-empty whitelist + unannotated target => refuse (negative).
    const std::string source = readFile(filePath);
    if (source.empty())
    {
        std::fprintf(stderr, "CodeGen: cannot read '%s'\n", filePath);
        return 2;
    }
    // Task 9.54 + 9.55 + 9.57: two views of the source, both with comments and INACTIVE preprocessor
    // branches blanked; `codeOnly` additionally blanks the inside of string and char literals. A marker
    // named only inside a comment, a string or a disabled `#if` branch must not count as an annotation, a
    // stray parenthesis in any of them must not unbalance the marker scan, and a literal containing the
    // block-comment opener must not be able to delete a real annotation.
    // Task 9.57: names defined on the command line count as defined for `#ifdef` / `#ifndef`.
    // Task 9.59: the annotation question is answered AFTER the parse, from the preprocessor's own list of
    // macro invocations (see collectAnnotations). Nothing is opened before that decision, so an
    // unannotated file still leaves no artifact.

    // "No half-written artifact" (goal item 2): emit into a sibling temp file and rename it onto the
    // requested path only after the emission actually succeeded. Before this, `-o` was opened with "w"
    // BEFORE the parse, so every non-zero exit (parse failure / type-not-found / refusal) left a 0-byte
    // file behind — and it also truncated any pre-existing content at that path. Measured red state of
    // the old code:
    //   CodeGen.exe Tools/CodeGen/TestPrivate.h PrivateLike -I CACore/header --emit-reflection -o <p>
    //   -> exit 5, and <p> exists with size 0.
    const bool  outIsFile = outPath != nullptr;
    std::string tmpPath;
    FILE*       out = stdout;
    if (outIsFile)
    {
        tmpPath = std::string(outPath) + ".tmp";
        out     = std::fopen(tmpPath.c_str(), "w");
        if (!out)
        {
            std::fprintf(stderr, "CodeGen: cannot open output '%s'\n", outPath);
            return 2;
        }
    }
    // Success path: close, then move the temp file onto the requested path. Returns FALSE when the
    // requested path could not actually be written — the caller MUST turn that into a non-zero exit.
    // (Found by the 9.12 adversarial review: the first version printed to stderr and still returned 0, so
    // a target that could not be replaced — a directory, a read-only file, or a file opened by another
    // process — kept its PREVIOUS content, left a `.tmp` behind, and exited 0. build.py only echoes
    // stderr when the exit code is non-zero, so the whole thing was invisible in the build log.)
    // NOTE: std::rename does NOT replace an existing destination on Windows (MSVC fails with EEXIST),
    // so the destination must be removed first.
    auto commitOut = [&]() -> bool {
        if (!outIsFile) return true;
        if (std::fclose(out) != 0)
        {
            std::fprintf(stderr, "CodeGen: could not flush '%s' — output not committed\n", tmpPath.c_str());
            std::remove(tmpPath.c_str());
            return false;
        }
        std::remove(outPath);
        if (std::rename(tmpPath.c_str(), outPath) != 0)
        {
            std::fprintf(stderr,
                "CodeGen: could not move '%s' onto '%s' — the requested output was NOT written\n",
                tmpPath.c_str(), outPath);
            std::remove(tmpPath.c_str());
            return false;
        }
        return true;
    };
    // Failure path: close and delete the temp file — the requested path is never created, and a
    // pre-existing file at that path keeps its old content.
    auto abortOut = [&] {
        if (!outIsFile) return;
        std::fclose(out);
        std::remove(tmpPath.c_str());
    };

    std::vector<const char*> args;
    args.push_back("-xc++"); // treat input as C++, not inferred by extension
    args.push_back("-std=c++20");
    for (const char* a : clangArgs)
        args.push_back(a);

    CXIndex index = clang_createIndex(/*excludeDeclsFromPCH*/ 1, /*displayDiagnostics*/ 0);
    CXTranslationUnit tu = nullptr;
    CXErrorCode err = clang_parseTranslationUnit2(
        index, filePath, args.data(), static_cast<int>(args.size()),
        /*unsaved_files*/ nullptr, /*num_unsaved*/ 0, CXTranslationUnit_DetailedPreprocessingRecord, &tu);

    if (err != CXError_Success)
    {
        std::fprintf(stderr, "libclang: parse failed (error=%d) for '%s'\n", static_cast<int>(err), filePath);
        abortOut();
        clang_disposeIndex(index);
        return 1;
    }

    // Task 9.59: WHICH TYPES ARE ANNOTATED comes from the preprocessor. collectAnnotations walks the
    // translation unit for the macro INVOCATIONS clang actually performed plus every file-scope and
    // namespace-scope struct/class in the file being parsed; an annotation binds to the first type that
    // follows it. A marker inside a comment, a string, a disabled preprocessor branch or an unused
    // `#define` body is not an invocation, so it simply is not in this list.
    std::vector<AnnotationUse>  uses;
    std::vector<TypeOccurrence> occurrences;
    {
        AnnotationCtx ctx{ filePath, &uses, &occurrences };
        clang_visitChildren(clang_getTranslationUnitCursor(tu), collectAnnotations, &ctx);
        for (AnnotationUse& u : uses)
            u.args = invocationArgs(source, u);
    }
    const std::map<std::string, AnnotationBinding> bound = bindAnnotations(uses, occurrences);
    if (bound.empty())
    {
        // Task 1.2: an EMPTY annotation list is only legitimate when the parse actually succeeded, so the
        // diagnostic scan runs BEFORE this early return. Measured before the fix: syntax_error.h,
        // missing_include.h and undefined_marker.h (evidence/fixtures/) all exited 0 with "no annotated
        // types", which is why a wrong compile environment could look exactly like a clean header.
        const DiagnosticScan scan = scanDiagnostics(tu, filePath);
        if (scan.untrustworthy())
        {
            std::fprintf(stderr,
                "CodeGen: refusing to report an empty annotation list for '%s' — the parse produced "
                "errors, so \"no annotated types\" cannot be trusted.\n", filePath);
            if (!scan.fatalMsg.empty())
                std::fprintf(stderr, "  unresolved include (fatal): %s\n", scan.fatalMsg.c_str());
            if (scan.inHeader > 0)
                std::fprintf(stderr, "  %u error-level diagnostic(s) inside the parsed header itself\n",
                             scan.inHeader);
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 6; // same meaning as the fidelity gate below: the parse cannot be trusted
        }
        std::fprintf(stderr, "CodeGen: no annotated types (empty whitelist) in '%s'; nothing to generate\n", filePath);
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 0; // spec.md "空白名单 -> EXIT 0 且不生成文件": no file opened, nothing emitted.
    }
    const std::vector<std::string> whitelist = [&bound] {
        std::vector<std::string> v;
        for (const auto& kv : bound) v.push_back(kv.first);
        return v;
    }();
    const bool targetWhitelisted = isWhitelisted(typeName, whitelist) ||
                                   (!qualifiedNameOverride.empty() && isWhitelisted(qualifiedNameOverride, whitelist));
    if (!targetWhitelisted)
    {
        std::fprintf(stderr,
            "CodeGen: type '%s' is not annotated (use CA_REFLECT / CA_CODEGEN_REGISTER / CA_REFLECTION); refusing to extract\n",
            typeName);
        abortOut();
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 4; // negative: an unmarked struct in a header that HAS marked types is not extracted.
    }

    // Task 6b.3 dual-mode: if the type is declared via `CA_REFLECTION(Type, a, b)` (the
    // unmigrated-engine + external transition channel), emit a fields-ONLY declaration-order table
    // from the macro's member list. This never edits the type and needs no libclang.
    {
        std::vector<std::string> members = scanCA_REFLECTIONList(bound, typeName);
        if (!members.empty())
        {
            const std::string qname = qualifiedNameOverride.empty() ? typeName : qualifiedNameOverride;
            // Task 9.30 + 9.43: this channel supports ONLY --emit-reflection. Any request that includes
            // serialization/hash is refused. The check must come FIRST: with `--emit-reflection
            // --emit-serialization` the reflection branch used to return before it, so the unsupported
            // flag was silently dropped and the run still exited 0.
            if (emitSerialization || emitHash)
            {
                std::fprintf(stderr,
                    "CodeGen: the CA_REFLECTION text channel does not support %s — it carries member "
                    "names only (no types/kinds), so the generated code would be unsound. Use "
                    "--emit-reflection for this channel, or annotate the type with CA_REFLECT.\n",
                    emitHash ? "--emit-hash" : "--emit-serialization");
                abortOut();
                return 8;
            }
            if (emitReflection)
            {
                // For this channel only the member NAMES are known, so offset comes from offsetof at
                // compile time and the type/kind are genuinely unknown (TypeKind::None + nullptr
                // typeName). Honest: the CA_REFLECTION macro carries no type/cursor info (design D2).
                std::vector<EmitField> ef;
                for (const std::string& m : members)
                    ef.push_back({ m, {}, "TypeKind::None" });
                emitTypeDescriptor(ef, qname, out);
                if (!commitOut())
                    return 7; // nothing to dispose yet: the text channel runs before the index exists
                return 0;
            }
            std::printf("%s %zu\n", typeName, members.size());
            for (const std::string& m : members)
                std::printf("  field %s\n", m.c_str());
            abortOut(); // nothing was written to the output path -> leave no artifact behind
            return 0;
        }
    }

    // Find the struct/class by the (last, unqualified) spelling. Enough for a first proof where the
    // whitelisted type name is unique; later may switch to USR/qualified matching for disambiguation.
    // NOTE: this runs BEFORE the fidelity gate below, because the gate's refusal report has to name
    // which members were degraded — that needs the type's field list.
    std::vector<std::pair<std::string, CXCursor>> structs;
    CollectCtx collect{ &structs };
    clang_visitChildren(clang_getTranslationUnitCursor(tu), collectStructs, &collect);

    CXCursor target{};
    bool found = false;
    bool foundDefinition = false;
    // Task 9.50①: prefer the DEFINITION cursor. The old loop took the FIRST same-named cursor, which for
    // a forward declaration (`struct Same;` above the marker) is a declaration with no members — the tool
    // then emitted an EMPTY specialization and still exited 0 (measured: `Same 0`, zero fields).
    for (auto& pr : structs)
    {
        if (pr.first != typeName)
            continue;
        const bool isDefinition = clang_isCursorDefinition(pr.second) != 0;
        if (!found || (isDefinition && !foundDefinition))
        {
            target          = pr.second;
            found           = true;
            foundDefinition = isDefinition;
        }
        if (foundDefinition)
            break;
    }
    if (!found)
    {
        std::fprintf(stderr, "libclang: type '%s' not found in '%s'\n", typeName, filePath);
        abortOut();
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 3;
    }
    // Task 9.55 (found by the round-32 adversarial review): preferring the definition cursor was not
    // enough. When the translation unit contains only a FORWARD declaration, `found` is true and
    // `foundDefinition` is false, and the tool still emitted a metadata table asserting
    // `fieldCount = 0` for a type it never saw defined — the same "empty artifact, exit 0" shape task
    // 9.50① was opened for, in a file whose generated header itself says the type must be fully defined.
    // Refuse instead of emitting a table that cannot be right.
    if (!foundDefinition)
    {
        std::fprintf(stderr,
            "CodeGen: refusing to emit for '%s' — only a DECLARATION of it exists in '%s', not a "
            "definition, so its member list would be emitted as empty. Include the header that defines "
            "the type (or pass that header as the input).\n",
            typeName, filePath);
        abortOut();
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 11;
    }

    FieldSink sink;
    std::vector<Field> fields;
    sink.fields = &fields;
    clang_visitChildren(target, collectFields, &sink);

    // Task 9.5b: fidelity gate — PROPERTY-based, never based on which flags were passed.
    // Measured truth (instrumented run; evidence file .review_tmp/9.5b-diagdump.txt):
    //   * a COMPLETE environment produces NO fatal diagnostic — only a `#pragma once in main file`
    //     warning, the MSVC STL1000 static_assert error inside yvals_core.h, and two unexpanded-pack
    //     errors inside CACore/header/Reflection.h. Those are benign for layout, so an absolute count of
    //     error-severity diagnostics can never be the gate: it would reject every legitimate input.
    //   * a DEGRADED environment (`-nostdinc++ -nostdinc`, `-nostdlibinc`, or a cross `--target` with
    //     SDK-only `-isystem`) produces a FATAL diagnostic — "'cstddef' file not found" inside the
    //     included framework header — after which the translation unit is truncated and every member
    //     past that point is silently reported with offset 0 and type `int`.
    // Hence the property gated here is: **the parse reached the end of the file we were asked to parse**.
    // A fatal diagnostic anywhere in the include chain means it did not, so the table cannot be trusted.
    // The gate reads diagnostics only — it never inspects the argument list.
    {
        // Task 1.2: the scan itself lives in scanDiagnostics() (shared with the empty-annotation check
        // above); this gate keeps only the fidelity-specific reporting below.
        const DiagnosticScan scan     = scanDiagnostics(tu, filePath);
        const std::string&   fatalMsg = scan.fatalMsg;
        const unsigned       inHeader = scan.inHeader;
        if (!fatalMsg.empty() || inHeader > 0)
        {
            std::fprintf(stderr,
                "CodeGen: refusing to extract from '%s' — the compile environment fed to this tool is "
                "incomplete, so the parsed AST is truncated and the type table would be silently wrong. "
                "Fidelity cannot be judged by the exit code.\n",
                filePath);
            if (!fatalMsg.empty())
                std::fprintf(stderr, "  unresolved include (fatal): %s\n", fatalMsg.c_str());
            if (inHeader > 0)
                std::fprintf(stderr, "  %u error-level diagnostic(s) inside the parsed header itself\n",
                             inHeader);
            // Name the members whose layout information is missing. Measured signature of the truncated
            // parse: a member that is NOT the first one reports offset 0 (it has no layout at all).
            std::string degraded;
            for (std::size_t k = 1; k < fields.size(); ++k)
            {
                if (fields[k].offsetBytes == 0)
                {
                    if (!degraded.empty()) degraded += ", ";
                    degraded += fields[k].name;
                }
            }
            if (!degraded.empty())
                std::fprintf(stderr,
                    "  degraded members of '%s' (reported offset 0 although not the first member): %s\n",
                    typeName, degraded.c_str());
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 6;
        }
    }

    // Emit in DECLARATION order — that is what clang_visitChildren already gives us, and it is the order
    // the emitters must use (task 9.5a). There is deliberately NO sort by offsetBytes here: the extracted
    // offset is produced by a config-sensitive parse (a degenerate env collapses every offset to 0), so
    // deriving the emission order from it would make the output depend on an unverified value — and with
    // all-zero offsets the old offset comparator was a no-op anyway (measured). Declaration order is
    // reproducible run to run, which is what the determinism oracle actually needs.

    // Task 5.1 reflection / Task 6.1 serialization emitter: the qualified name comes from the AST
    // (or an explicit --qualified-name override) — the CA_REFLECT marker has no type name, so the tool
    // derives it from where the type actually lives. Both share the per-field metadata.
    const std::string qname =
        qualifiedNameOverride.empty() ? buildQualifiedName(target) : qualifiedNameOverride;

    // Task 9.1: refuse to EMIT for a type with non-public members on the outer-class channel. Emitting
    // would produce a header that names private members (offsetof on them) and therefore cannot compile
    // — the old behaviour emitted it anyway and the failure surfaced far away, as C2248 in some consumer
    // TU. Extraction (no --emit-*) above this point is untouched: printing the type table for a private
    // type is legitimate and stays exit 0.
    if ((emitSerialization || emitHash || emitReflection) && sink.sawNonPublic)
    {
        std::fprintf(stderr,
            "CodeGen: refusing to emit for '%s' — it has non-public members and the outer-class "
            "annotation channel (CA_REFLECTION / CA_REFLECT at namespace scope) can only serve types "
            "whose members are all public. Use an in-class annotation (macro inside the class body, "
            "expanding to a friend declaration) instead.\n",
            qname.c_str());
        abortOut();
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 5;
    }

    // Task 9.43: exactly one emitter per run. Before this, `--emit-serialization --emit-hash` emitted
    // only the first one, dropped the other and exited 0 (measured: 1203 B, no `custom_hash_trait`).
    {
        const int emitterCount = (emitReflection ? 1 : 0) + (emitSerialization ? 1 : 0) + (emitHash ? 1 : 0);
        if (emitterCount > 1)
        {
            std::fprintf(stderr,
                "CodeGen: refusing — %d emitter flags in one run; --emit-reflection / --emit-serialization "
                "/ --emit-hash are mutually exclusive per invocation. Invoke the tool once per emitter "
                "(build.py already does).\n",
                emitterCount);
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 10;
        }
    }

    // Task 9.31: the `serializable` marker option is what the design says gates this emitter
    // ("only types marked `serializable` get serialization code"). It was never read: ANY annotated type
    // got a `CodeGenSerializer<T>` specialization, so the documented gate did not exist. Read the
    // marker text now and refuse when the option is absent.
    if (emitSerialization)
    {
        const std::string opts = scanMarkerOptions(bound, typeName);
        if (!hasMarkerOption(opts, "serializable"))
        {
            std::fprintf(stderr,
                "CodeGen: refusing --emit-serialization for '%s' — the type's CA_REFLECT marker does not "
                "carry the `serializable` option (marker options seen: '%s'). Add `serializable` to the "
                "marker, or emit reflection only.\n",
                qname.c_str(), opts.c_str());
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 9;
        }
    }

    // Task 4.1 (1): the canonical order of an unordered container needs `<` on the sorting side. A type
    // with no ordering operator at all has none, so the promised "same bytes for the same content" could
    // not be delivered. (The criterion accepts `operator<=>`; see hasOrderingOperator.)
    if (emitSerialization || emitHash)
    {
        for (const Field& f : fields)
        {
            if (!f.sortSideNotOrdered)
                continue;
            std::fprintf(stderr,
                "CodeGen: refusing %s for '%s' — member '%s': an unordered container is sorted before it "
                "is written, but its sorting side has no ordering operator (neither `operator<` nor "
                "`operator<=>`), so the canonical order is undefined. Give the type an ordering operator, "
                "or use an ordered container with an explicit comparator.\n",
                (emitSerialization ? "--emit-serialization" : "--emit-hash"),
                qname.c_str(), f.name.c_str());
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 17;
        }
    }

    // Task 4.1 (4): the generated reader builds each key / element with `T t{}` before reading into it,
    // so a type without a usable default constructor cannot be read back. Refuse rather than emit code
    // that does not compile.
    if (emitSerialization || emitHash)
    {
        for (const Field& f : fields)
        {
            if (!(f.keyNotDefault || f.elementNotDefault))
                continue;
            std::fprintf(stderr,
                "CodeGen: refusing %s for '%s' — member '%s': the %s type '%s' has no usable default "
                "constructor, and the generated reader builds a value with `T t{}` before reading into "
                "it. Give the type a default constructor, or use a container of a type that has one.\n",
                (emitSerialization ? "--emit-serialization" : "--emit-hash"),
                qname.c_str(), f.name.c_str(),
                (f.keyNotDefault ? "key" : "element"),
                (f.keyNotDefault ? f.keyTypeName.c_str() : f.elementTypeName.c_str()));
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 16;
        }
    }

    // Task 4.1 (2): a container whose element is a raw pointer, a managed pointer or a handle cannot be
    // written faithfully - the old runtime ignores such elements, so the payload silently loses data.
    // Refuse when serialization or hashing is requested instead of shipping a lossy stream.
    if (emitSerialization || emitHash)
    {
        for (const Field& f : fields)
        {
            if (!(f.isContainer && (f.elementIsOpaque || f.elementIsManagedPointer)))
                continue;
            std::fprintf(stderr,
                "CodeGen: refusing %s for '%s' — member '%s' is a container whose element is a raw or "
                "managed pointer: the old runtime does not write pointer elements, so the stream would "
                "silently lose them. Store values (or an index/handle plus a table on the side).\n",
                (emitSerialization ? "--emit-serialization" : "--emit-hash"),
                qname.c_str(), f.name.c_str());
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 15;
        }
    }

    // Task 4.1 (3): a duplicate-key container (`multimap` / `multiset` / the unordered variants) has no
    // canonical order among equal keys, and this change deliberately does not invent a secondary sort
    // key, so the canonical write order is undefined. Refuse instead of writing an order-dependent stream.
    if (emitSerialization || emitHash)
    {
        for (const Field& f : fields)
        {
            if (!f.containerIsMultiKey)
                continue;
            std::fprintf(stderr,
                "CodeGen: refusing %s for '%s' — member '%s' is a duplicate-key container ('%s'): equal "
                "keys have no canonical order, and this generator does not define a secondary sort key. "
                "Use a unique-key container, or serialize a container of key/value pairs instead.\n",
                (emitSerialization ? "--emit-serialization" : "--emit-hash"),
                qname.c_str(), f.name.c_str(), f.typeName.c_str());
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 14;
        }
    }

    // Task 2.1 / 4.1 (5): a bit-packed container (`vector<bool>`) satisfies the structural criterion
    // (it has `size()` and `value_type`) but not the old runtime's `for (auto& item : object)` loop:
    // the iterator yields a proxy object and `auto&` cannot bind to it, so instantiating the emitted
    // code is a hard compile error (C2440). Refuse instead of emitting code that cannot compile.
    if (emitSerialization || emitHash)
    {
        for (const Field& f : fields)
        {
            if (!f.containerIsBitPacked)
                continue;
            std::fprintf(stderr,
                "CodeGen: refusing %s for '%s' — member '%s' is a bit-packed container ('%s'): its "
                "elements are stored packed, so the old runtime's element loop cannot bind a reference to "
                "them. Use a container of bytes (or widen the element type) instead.\n",
                (emitSerialization ? "--emit-serialization" : "--emit-hash"),
                qname.c_str(), f.name.c_str(), f.typeName.c_str());
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 13;
        }
    }

    // Task 3.2 (1): a container element that is a STRUCT holding an unordered container cannot be
    // written in canonical order by the generated recursion (see findUnorderedInside), so refuse the
    // serialization emitter instead of shipping code whose bytes depend on insertion order.
    if (emitSerialization || emitHash)
    {
        for (const Field& f : fields)
        {
            if (f.elementUnstableInside.empty())
                continue;
            std::fprintf(stderr,
                "CodeGen: refusing %s for '%s' — member '%s' is a container whose element "
                "type '%s' itself holds the unordered container '%s'. The generated code cannot recurse into "
                "another type's fields (the old runtime keeps its read cursor private), so that element would "
                "be written in traversal order and the bytes would depend on insertion history. Either give the "
                "element type its own ordering-free shape (no unordered container inside), or drop the member "
                "from the serialized set.\n",
                (emitSerialization ? "--emit-serialization" : "--emit-hash"),
                qname.c_str(), f.name.c_str(), f.elementTypeName.c_str(), f.elementUnstableInside.c_str());
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 12;
        }
    }

    std::vector<EmitField> ef;
    for (const Field& f : fields)
    {
        EmitField e{ f.name, f.typeName, fieldTypeKindEnum(static_cast<CXTypeKind>(f.typeKind)) };
        e.isContainer        = f.isContainer;
        e.unordered          = f.containerUnordered;
        e.bitPacked          = f.containerIsBitPacked;
        e.hasOpaqueElement   = f.elementIsOpaque || f.elementIsManagedPointer;
        e.wrapper            = f.elementIsWrapper || f.keyIsWrapper;
        e.element2TypeName   = f.element2TypeName;
        e.element2IsContainer = f.element2IsContainer;
        e.element2KindName   = f.element2TypeName.empty()
                                   ? ""
                                   : fieldTypeKindEnum(static_cast<CXTypeKind>(f.element2Kind));
        e.keyTypeName        = f.keyTypeName;
        e.keyKindName        = f.keyTypeName.empty() ? "" : fieldTypeKindEnum(static_cast<CXTypeKind>(f.keyKind));
        e.elementTypeName    = f.elementTypeName;
        e.elementKindName    = f.elementTypeName.empty() ? "" : fieldTypeKindEnum(static_cast<CXTypeKind>(f.elementKind));
        e.elementIsContainer = f.elementIsContainer;
        ef.push_back(e);
    }
    if (emitSerialization)
    {
        emitCodeGenSerializer(ef, qname, out);
        if (!commitOut())
        {
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 7;
        }
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 0;
    }
    if (emitHash)
    {
        emitHashTrait(ef, qname, out);
        if (!commitOut())
        {
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 7;
        }
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 0;
    }
    if (emitReflection)
    {
        emitTypeDescriptor(ef, qname, out);
        if (!commitOut())
        {
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 7;
        }
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 0;
    }

    std::printf("%s %zu\n", typeName, fields.size());
    for (const Field& f : fields)
        std::printf("  field %s %zu %s %d container=%d unordered=%d map=%d set=%d multikey=%d "
                    "bitpacked=%d key=%s keyKind=%d element=%s "
                    "elementKind=%d elemContainer=%d elemOpaque=%d elemManagedPtr=%d\n",
                    f.name.c_str(), f.offsetBytes, f.typeName.c_str(), f.typeKind,
                    f.isContainer ? 1 : 0, f.containerUnordered ? 1 : 0, f.containerIsMap ? 1 : 0,
                    f.containerIsSet ? 1 : 0, f.containerIsMultiKey ? 1 : 0,
                    f.containerIsBitPacked ? 1 : 0, f.keyTypeName.c_str(), f.keyKind,
                    f.elementTypeName.c_str(), f.elementKind,
                    f.elementIsContainer ? 1 : 0, f.elementIsOpaque ? 1 : 0,
                    f.elementIsManagedPointer ? 1 : 0);

    // Task 6b.2: carry CA_REFLECT marker options (from source text) into the output.
    const std::string options = scanMarkerOptions(bound, typeName);
    if (!options.empty())
        std::printf("# options: %s\n", options.c_str());

    abortOut(); // no --emit-*: the table went to stdout, so the output path stays untouched
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    return 0;
}
