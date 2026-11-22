// CodeGenMain.cpp — minimal libclang-driven extraction tool for the CodeGen framework.
//
// Takes a header file + a type name, parses it with libclang (the real C++ front end — we are
// NOT hand-writing a parser), finds the struct/class of that name, and emits a deterministic
// linear field table (one line per field, in DECLARATION order — removed the sort by
// extracted byte offset, which was built on a value a degenerate environment silently collapses to 0):
//
//     field <name> <offsetBytes> <typeSpelling> <CXTypeKind>
//
// This is the proof-of-pipeline for parse -> cursor traversal -> type table, and
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
#include <functional>
#include <initializer_list>
#include <iterator>
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
        // container facts, recognized structurally (see analyzeContainer).
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
        bool        elementIsWrapper = false;   // recorded only, never opaque
        bool        memberIsOpaque = false;          // the member ITSELF is a raw pointer
        bool        memberIsManagedPointer = false;  // the member ITSELF is a shared_ptr / unique_ptr
        bool        keyNotDefault = false;       // (4)
        bool        sortSideNotOrdered = false; // (1)
        bool        elementNotDefault = false;
        // for `int a[2][3]` the element is `int[3]`, and THIS is its own element (`int`).
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
     * containers are recognized STRUCTURALLY — exactly the rule the runtime uses
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
     * kept aligned by the parity assertion in the tests, not by this probe.
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

    /* (1): does the type carry an ordering operator? The canonical write order for an unordered
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

    /* (4): can a value of this type be built with `T t{}`? The generated reader does exactly
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

    /* the second level of a nested description. For `vector<vector<int>>` the element is
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

    /* the engine's wrappers. Detection is BY NAME because what makes them wrappers -- the runtime's
     * wrapper-trait specialization (`cacore::codegen::wrapper_traits`) -- is not visible to libclang
     * through its C API. The three names are the ones the engine actually uses today; a new wrapper
     * type has to be added here, which is a known boundary.
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

    void analyzeContainer(CXType spelled, Field& f)
    {
        const CXType canon = stripElaborated(clang_getCanonicalType(spelled));
        if (canon.kind == CXType_ConstantArray || canon.kind == CXType_IncompleteArray)
        {
            // / 3.2 (3): a C array is a container by EXTENT. Its element is the type with one
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
            // 只有**参与排序的那一侧**需要全序：map 的规范顺序按**键**排（生成的比较器就是
            // `a.first < b.first`），值不参与排序，所以值没有排序算子不算问题。
            // 这一条以前把值那侧也算进来，结果是引擎里真实的 `unordered_map<sha256 摘要, VulkanShaderCode>`
            // 被拒（值是个带向量的结构体，没有排序算子），而它本来完全写得出来。
            if (f.containerUnordered && !hasOrderingOperator(k.second))
                f.sortSideNotOrdered = true;
            f.keyNotDefault     = !isDefaultConstructible(k.second);
            if (f.elementIsContainer)
            {
                bool secondIsContainer = false;
                describeElementOfContainer(v.second, f.element2TypeName, f.element2Kind, secondIsContainer);
                f.element2IsContainer = secondIsContainer;
            }
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
            // Candidate types: file scope, namespace scope, and types nested inside another
            // struct/class (a nested type is a real delivery target — the engine annotates
            // `DepthStencilStates::StencilStates` and `cahash::sha256_hash::result_type`).
            // A type declared INSIDE A FUNCTION is still excluded: the visitor recurses into
            // functions, and a helper struct declared there used to be pooled with the namespace-level
            // type of the same name; the tool then emitted the local struct's member list for the
            // annotated global type (measured: `Foo 1 / field inner` for a global
            // `Foo { int a; int b; }` that carried the annotation).
            const CXCursorKind parentKind = clang_getCursorKind(clang_getCursorSemanticParent(cursor));
            const bool scoped = parentKind == CXCursor_TranslationUnit || parentKind == CXCursor_Namespace ||
                                parentKind == CXCursor_StructDecl || parentKind == CXCursor_ClassDecl;
            if (scoped)
            {
                CXString spelling = clang_getCursorSpelling(cursor);
                ctx->out->emplace_back(clang_getCString(spelling), cursor);
                clang_disposeString(spelling);
            }
            // Recurse either way: a nested type is declared INSIDE its enclosing class body, so
            // not descending would hide exactly the types this branch now accepts.
            return CXChildVisit_Recurse;
        }
        return CXChildVisit_Recurse;
    }

    struct FieldSink
    {
        std::vector<Field>* fields       = nullptr;
        // the OUTER-class annotation channel (`CA_REFLECT` written before the type, i.e. at namespace
        // scope) is a zero-intrusion text scan; the code it emits names members directly, so it can only
        // serve types whose members are ALL public. Extraction (printing the type table) is still fine for
        // private members — only EMITTING compilable code is refused. See the guard before the emitters.
        bool                sawNonPublic = false;
        // 类里有没有把生成的入口加成友元（`CA_CODEGEN_FRIEND(T)` 展开出来的那四条 friend 声明）。
        // 有它的时候，生成代码按成员名读私有成员是合法的，上面那条拒绝就该放行。
        bool                sawCodegenFriend = false;
    };

    enum CXChildVisitResult collectFields(CXCursor cursor, CXCursor /*parent*/, CXClientData data)
    {
        auto* sink = static_cast<FieldSink*>(data);
        // 类内的友元声明：`CA_CODEGEN_FRIEND(T)` 展开成四条 `friend struct ::cacore::codegen::X<T>;`，
        // libclang 把它们报成 FriendDecl。判据取"被友元化的那个类型的拼写里点没点名四个入口之一" ——
        // 读的是 AST，不做源码文本扫描（文本扫描对注释、字符串、`#if 0` 都不可靠，见 README 第三节）。
        // 两个拼写都查：有些 clang 版本把名字放在游标拼写上，有些放在类型拼写上。
        if (clang_getCursorKind(cursor) == CXCursor_FriendDecl)
        {
            // libclang 的 C API 对"友元的是谁"几乎不给信息：游标拼写、类型拼写、`clang_getCursorReferenced`
            // 全是空的（实测 usr 只有 `c:`）。所以用 `clang_getCursorPrettyPrinted` 把这条声明打印出来 ——
            // 打印的是**这条声明自己**的源码形态（宏调用处就打印宏调用，展开后的声明就打印展开后的样子），
            // 不是对整份文件做文本扫描。
            CXPrintingPolicy policy = clang_getCursorPrintingPolicy(cursor);
            CXString         pretty = clang_getCursorPrettyPrinted(cursor, policy);
            const char*      prettyText = clang_getCString(pretty);
            const std::string text = prettyText ? prettyText : "";
            clang_disposeString(pretty);
            clang_PrintingPolicy_dispose(policy);
            if (text.find("CA_CODEGEN_FRIEND") != std::string::npos ||
                text.find("codegen::TypeDescriptor") != std::string::npos ||
                text.find("codegen::CodeGenSerializer") != std::string::npos ||
                text.find("codegen::CodeGenEqual") != std::string::npos ||
                text.find("codegen::custom_hash_trait") != std::string::npos)
                sink->sawCodegenFriend = true;
            return CXChildVisit_Continue;
        }
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
            analyzeContainer(type, f);   // container facts, recognized structurally
            // Does the member ITSELF (not its element) point at something? `shared_ptr<T>` / `unique_ptr<T>`
            // are records, so the kind test alone misses them — the spelling test covers those.
            const CXType canonicalMember = clang_getCanonicalType(type);
            f.memberIsOpaque             = (canonicalMember.kind == CXType_Pointer);
            f.memberIsManagedPointer     = looksLikeManagedPointer(canonicalMember);

            clang_disposeString(sName);
            clang_disposeString(sType);
            sink->fields->push_back(std::move(f));
        }
        return CXChildVisit_Continue;
    }

    // --- read CA_REFLECT marker options from the source TEXT (not libclang attributes).
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

    /** + 9.50②: match a marker option as a WHOLE `name[=value]` pair.
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

    /** WHICH TYPES ARE ANNOTATED is now answered by the preprocessor, not by scanning text.

        Three review rounds in a row each closed one spelling by which a
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
        std::string macro; // CA_REFLECT / CA_CODEGEN_REGISTER
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
            if ((name == "CA_REFLECT" || name == "CA_CODEGEN_REGISTER") &&
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
            // Candidate types: file scope, namespace scope, and types nested inside another
            // struct/class. A type declared inside a FUNCTION is not a delivery target.
            // Class templates are recorded as well, so that a file which annotates ONLY a template is not
            // reported as "no annotated types"; extracting one still fails later, because the target
            // search knows only struct/class cursors.
            const CXCursorKind parentKind = clang_getCursorKind(clang_getCursorSemanticParent(cursor));
            const bool scoped = parentKind == CXCursor_TranslationUnit || parentKind == CXCursor_Namespace ||
                                parentKind == CXCursor_StructDecl || parentKind == CXCursor_ClassDecl;
            if (scoped && cursorIsInMainFile(cursor, ctx->filePath))
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
            // Recurse: a nested type is declared inside its enclosing class body, so not descending
            // would hide exactly the types the branch above now accepts.
            return CXChildVisit_Recurse;
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

        `CA_CODEGEN_REGISTER("Qualified.Name")` carries the annotated type's NAME instead of sitting next to
        its definition, so it resolves by name (the string literal); `CA_REFLECT` binds to the type
        declaration that follows the invocation. */
    std::map<std::string, AnnotationBinding> bindAnnotations(const std::vector<AnnotationUse>& uses,
                                                             const std::vector<TypeOccurrence>& types)
    {
        std::map<std::string, AnnotationBinding> bound;
        for (const AnnotationUse& u : uses)
        {
            std::string named;
            if (u.macro == "CA_CODEGEN_REGISTER")
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

    // --- reflection emitter. The tool does NOT link CodeGenMeta.h (it is a pure
    // libclang text producer, not part of the engine build), so it emits *enum-NAME strings*
    // (e.g. "TypeKind::Int") and relies on the consumer TU to resolve them against cacore::codegen.
    struct EmitField
    {
        std::string name;     // member name, e.g. "a"
        std::string typeName; // spelled C++ type, e.g. "float"; empty => emit nullptr (unknown type)
        std::string kindName; // semantic TypeKind::X enum-name string
        // container facts copied from the analysis, emitted as flags + element/key tables.
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

    // 一个类型一份产物（`--emit-all --out-dir`）时，文件头在**所有节之前**写一次：说明注释一次、
    // `#pragma once` 一次、运行时头的 include 各一次。三节各自的注释块照旧留在自己那一节的开头
    // （每节都保留 `// Generated by CodeGen <某个> emitter for <类型>` 那一行）。只有这个模式会走到这里，
    // 而它只能由 `--emit-all` 打开 —— 那时三节全是开的，所以文件头列三个运行时头是准的。
    void emitMergedPreamble(const std::string& qname, FILE* out)
    {
        std::fprintf(out,
            "// Generated by CodeGen for %s — one file per type: reflection, serialization, hash.\n"
            "// Requires: the reflected type is fully defined (for offsetof) and the CodeGen runtime headers\n"
            "// below are on the include path. Deterministic: same input => same bytes.\n"
            "#pragma once\n"
            "\n"
            "#include \"CodeGenMeta.h\"\n"
            "#include \"CodeGenSerialization.h\"\n"
            "#include \"CodeGenHash.h\"\n"
            "\n",
            qname.c_str());
    }

    // Emit a readable, constexpr TypeDescriptor<T> specialization (the shape the ABI guard / runtime
    // readers consume). Deterministic: the field list arrives in DECLARATION order and is emitted as-is
    // (no sort — see ), so the same input always yields the same bytes.
    void emitTypeDescriptor(const std::vector<EmitField>& fields, const std::string& qname, FILE* out,
                            bool merged)
    {
        std::fprintf(out,
            "// Generated by CodeGen reflection emitter for %s.\n"
            "// Requires: the reflected type is fully defined (for offsetof) and CodeGenMeta.h is on\n"
            "// the include path, before this fragment. Deterministic: same input => same bytes.\n",
            qname.c_str());
        // 合并模式里这一节只是同一个文件里的一节，`#pragma once` 归文件头 —— 这里再写一次就是重复。
        if (!merged)
            std::fprintf(out, "#pragma once\n\n");
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
            // one-entry descriptor tables for container members. `offset` is 0 because an
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

    // --- Serialization emitter. Generates a `CodeGenSerializer<T>` that (de)serializes the
    // reflected type field-by-field in DECLARATION ORDER (the order the type table carries).
    // The generated code is a thin specialization over the
    // CodeGen-owned runtime (CACore/header/CodeGenSerialization.h): the writer appends, the reader carries
    // an EXPLICIT position reference, so the generated code can hand its position to another type's
    // generated code and keep reading — which is what makes "a container of structs" expressible.
    // Gated on the `serializable` marker option
    // (design AUDIT-4, decided 2026-09-08): only emitted for serializable types.
    void emitCodeGenSerializer(const std::vector<EmitField>& fields, const std::string& qname, FILE* out,
                               bool merged)
    {
        std::fprintf(out,
            "// Generated by CodeGen serialization emitter for %s.\n"
            "// Requires: the type is defined + CodeGenSerialization.h on the include path.\n"
            "// Deterministic: declaration-order field traversal, same input => same bytes.\n"
            "// Unordered containers are flattened and sorted before writing, so the bytes depend on the\n"
            "// CONTENT only - never on the insertion history of the container.\n",
            qname.c_str());
        // 合并模式：`#pragma once` 与 `CodeGenSerialization.h` 的 include 归文件头。
        if (!merged)
            std::fprintf(out, "#pragma once\n\n#include \"CodeGenSerialization.h\"\n\n");
        std::fprintf(out,
            "namespace cacore::codegen\n"
            "{\n"
            "template<>\n"
            "struct CodeGenSerializer<%s>\n"
            "{\n"
            "    using Obj = %s;\n"
            "    template<typename ByteBuffer>\n"
            "    static void serialize(Obj const& obj, ByteBuffer& buffer)\n"
            "    {\n"
            "        ::cacore::codegen::writer<ByteBuffer> w(buffer);\n",
            qname.c_str(), qname.c_str());
        for (const EmitField& f : fields)
            std::fprintf(out, "        cgWriteValue(w, obj.%s);\n", f.name.c_str());
        std::fprintf(out,
            "    }\n"
            "    // The read position is an explicit parameter: this entry can be called in the middle of a\n"
            "    // stream, and it leaves `position` where it stopped so the caller keeps reading.\n"
            "    template<typename ByteBuffer>\n"
            "    static bool deserialize(Obj& obj, ByteBuffer const& buffer, std::size_t& position)\n"
            "    {\n"
            "        ::cacore::codegen::reader_t<ByteBuffer> r(buffer, position);\n");
        for (const EmitField& f : fields)
            std::fprintf(out, "        if (!cgReadValue(r, obj.%s)) return false;\n", f.name.c_str());
        std::fprintf(out,
            "        return r.ok();\n"
            "    }\n"
            "};\n"
            "} // namespace cacore::codegen\n");
    }

    // --- Hash + equality emitter. Generates a `custom_hash_trait<T>` (the hook the CodeGen
    // hasher calls) plus a `CodeGenEqual<T>` (the same-source equality: whatever the hash walks, the
    // equality walks). It hashes EACH FIELD in declaration order, NOT the whole struct's raw bytes
    // (=> padding/layout independent), and normalizes floats (-0.0 -> +0.0, every NaN -> a canonical NaN).
    // All of that already lives in the runtime's `cgHashContainerValue` / `cgEqualContainer` /
    // `cgEqualValue`, so the emitted file is a thin specialization.
    void emitHashTrait(const std::vector<EmitField>& fields, const std::string& qname, FILE* out,
                       bool merged)
    {
        std::fprintf(out,
            "// Generated by CodeGen hash/equality emitter for %s.\n"
            "// Field-wise (padding/layout independent), floats normalized (-0.0=+0.0, NaN canonical).\n"
            "// Requires: type defined + CodeGenHash.h on the include path. Deterministic (declaration order).\n",
            qname.c_str());
        // 合并模式：`#pragma once` 与 `CodeGenHash.h` 的 include 归文件头。
        if (!merged)
            std::fprintf(out, "#pragma once\n\n#include \"CodeGenHash.h\"\n\n");
        std::fprintf(out,
            "namespace cacore::codegen\n"
            "{\n"
            "template<>\n"
            "struct CodeGenEqual<%s>\n"
            "{\n"
            "    using Obj = %s;\n"
            "    static bool equal(Obj const& a, Obj const& b)\n"
            "    {\n",
            qname.c_str(), qname.c_str());
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
                // `a.arr != b.arr` on a C array compares ADDRESSES (array-to-pointer decay), so container
                // members are compared element-wise over both full ranges.
                std::fprintf(out,
                    "        if (!cgEqualContainer(a.%s, b.%s)) return false;\n",
                    f.name.c_str(), f.name.c_str());
            }
            else
            {
                // Same source as the hash: a nested annotated type compares through ITS generated equal,
                // a scalar through `==`.
                std::fprintf(out, "        if (!cgEqualValue(a.%s, b.%s)) return false;\n",
                             f.name.c_str(), f.name.c_str());
            }
        }
        std::fprintf(out,
            "        return true;\n"
            "    }\n"
            "};\n"
            "\n"
            "template<>\n"
            "struct custom_hash_trait<%s>\n"
            "{\n"
            "    using Obj = %s;\n"
            "    template<typename hasher_t>\n"
            "    static void hash(Obj const& obj, hasher_t& h)\n"
            "    {\n",
            qname.c_str(), qname.c_str());
        for (const EmitField& f : fields)
            std::fprintf(out, "        cgHashContainerValue(h, obj.%s);\n", f.name.c_str());
        std::fprintf(out,
            "    }\n"
            "    static bool equal(Obj const& a, Obj const& b) { return CodeGenEqual<%s>::equal(a, b); }\n"
            "};\n"
            "} // namespace cacore::codegen\n",
            qname.c_str());
    }

    // --- whitelist gating ("标过了才生成").
    // The tool must NOT reflect every struct it parses — only types that carry a discovery marker.
    // We scan the SOURCE TEXT (not libclang attributes, which libclang's C API reads weakly) for the
    // two marker forms and build the whitelist of "cross-the-boundary" type names:
    //   (a) CA_CODEGEN_REGISTER("QualifiedName")   -- string-literal arg (so :: is fine)
    //   (b) CA_REFLECT(...)                         -- type is the class/struct body FOLLOWING the marker
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

// the diagnostic scan, factored out so it can also run BEFORE the "no annotated types" early
// return. Measured defect it fixes: a syntax error, an unresolvable `#include`, and a marker macro that
// was never defined all produced ZERO annotations, and the tool then printed "no annotated types (empty
// whitelist)" and exited 0 — so a broken parse was indistinguishable from a clean header that has
// nothing to generate, and a wrong compile environment looked like success.
struct DiagnosticScan
{
    std::string fatalMsg;      // first fatal diagnostic (unresolved include and friends)
    unsigned    inHeader = 0;  // error-level diagnostics inside the file we were asked to parse

    // The error-level diagnostics themselves, formatted, in order (capped). Without these the gate
    // could only say "the environment is incomplete" — the one thing the reader needs is WHICH line and
    // WHAT the compiler said.
    std::vector<std::string> headerErrors;
    // 同一批错误，但不限文件：被解析的头 include 的生成头 / 引擎头里的第一处错误往往才是根因
    // （实测：`too many errors emitted, stopping now` 时，头内计数恒为 0，只看 headerErrors 什么都看不到）。
    std::vector<std::string> anyErrors;

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
            if (scan.anyErrors.size() < 5)
            {
                CXString msg = clang_formatDiagnostic(d, clang_defaultDiagnosticDisplayOptions());
                const char* c = clang_getCString(msg);
                scan.anyErrors.push_back((c != nullptr) ? c : "(unformattable)");
                clang_disposeString(msg);
            }
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
                if (same)
                {
                    ++scan.inHeader;
                    if (scan.headerErrors.size() < 5)
                    {
                        CXString msg = clang_formatDiagnostic(d, clang_defaultDiagnosticDisplayOptions());
                        const char* c = clang_getCString(msg);
                        scan.headerErrors.push_back((c != nullptr) ? c : "(unformattable)");
                        clang_disposeString(msg);
                    }
                }
            }
        }
        clang_disposeDiagnostic(d);
    }
    return scan;
}

// Everything this parse actually READ, as libclang saw it. In the three-products mode it is printed as
// `CodeGen deps: <path>` lines so build.py can hash those files into its "inputs unchanged" ledger.
//
// Why bother: the emitted bytes depend on the WHOLE include closure, not on the one header the manifest
// names. Editing a header that a manifest header includes (but that is not itself in the manifest) used to
// leave the ledger saying "nothing changed" -> the row was skipped -> the compiler kept consuming the
// previous products while the build reported success. Re-deriving the list from the source text would be a
// second, weaker copy of the preprocessor, so it comes from libclang instead.
struct InclusionCollector
{
    std::vector<std::string>* paths;
};

void collectInclusion(CXFile includedFile, CXSourceLocation* /*stack*/, unsigned /*depth*/,
                      CXClientData clientData)
{
    auto* collector = static_cast<InclusionCollector*>(clientData);
    CXString name = clang_getFileName(includedFile);
    const char* text = clang_getCString(name);
    if (text != nullptr && *text != '\0')
        collector->paths->push_back(text);
    clang_disposeString(name);
}

// =============================================================================================
// 绑定发射器（`--emit-binding`）：把被 `CA_BIND` 标注的接口投影成 C ABI 声明 + C# 声明。
// 规矩见 openspec/specs/csharp-bindable-surface/spec.md 与 Tools/CodeGen/README.md 第十三节。
// 这一段只出**签名**（任务 2.1）：thunk 实现、版本化函数表、句柄表、布局清单在后续任务接。
// =============================================================================================
namespace bindemit
{
    std::string toStd(CXString value)
    {
        const char* text = clang_getCString(value);
        std::string result = text != nullptr ? text : "";
        clang_disposeString(value);
        return result;
    }

    std::string trimCopy(std::string value)
    {
        const std::size_t begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
        {
            return {};
        }
        const std::size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }

    std::string bareType(std::string spelling)
    {
        spelling = trimCopy(std::move(spelling));
        for (;;)
        {
            if (spelling.rfind("const ", 0) == 0)
            {
                spelling = trimCopy(spelling.substr(6));
                continue;
            }
            if (!spelling.empty() && (spelling.back() == '&' || spelling.back() == '*'))
            {
                const char last = spelling.back();
                spelling.pop_back();
                spelling = trimCopy(spelling);
                if (last == '&' && !spelling.empty() && spelling.back() == '&')
                {
                    spelling.pop_back();
                    spelling = trimCopy(spelling);
                }
                continue;
            }
            break;
        }
        return spelling;
    }

    bool isScalarType(std::string const& bare)
    {
        static const char* const scalars[] = {
            "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t",
            "int64_t", "uint64_t", "float", "double", "bool", "size_t",
            // 真实接口里写的是 C 的原生类型（`int` / `unsigned int` / `char` …），一样按定宽整数投影
            "int", "unsigned", "unsigned int", "long", "unsigned long", "short", "char",
        };
        for (char const* scalar : scalars)
        {
            if (bare == scalar)
            {
                return true;
            }
        }
        return false;
    }

    // 标量宽度类型的字节数（标量容器两段式的拷贝容量要按字节算）。
    std::size_t scalarWidthBytes(std::string const& widthType)
    {
        if (widthType == "uint8_t" || widthType == "int8_t" || widthType == "bool") return 1u;
        if (widthType == "uint16_t" || widthType == "int16_t") return 2u;
        if (widthType == "uint32_t" || widthType == "int32_t" || widthType == "float") return 4u;
        if (widthType == "uint64_t" || widthType == "int64_t" || widthType == "double") return 8u;
        return 0u;
    }

    // 标量拼写 → 定宽宽度类型（返回位置标量容器两段式要用，本批 1.6 / design D13）。
    // 只认显式列出的拼写；认不出就返回空串，由调用方生成期点名（不猜宽度）。
    std::string normalizeScalarSpelling(std::string const& spelling)
    {
        const std::string bare = bareType(spelling);
        if (bare == "uint8_t" || bare == "unsigned char" || bare == "char") return "uint8_t";
        if (bare == "int8_t" || bare == "signed char") return "int8_t";
        if (bare == "uint16_t" || bare == "unsigned short") return "uint16_t";
        if (bare == "int16_t" || bare == "short") return "int16_t";
        if (bare == "uint32_t" || bare == "unsigned int" || bare == "unsigned") return "uint32_t";
        if (bare == "int32_t" || bare == "int") return "int32_t";
        if (bare == "uint64_t" || bare == "unsigned long" || bare == "unsigned long long" ||
            bare == "size_t")
            return "uint64_t";
        if (bare == "int64_t" || bare == "long" || bare == "long long") return "int64_t";
        if (bare == "float") return "float";
        if (bare == "double") return "double";
        if (bare == "bool") return "bool";
        return std::string();
    }

    bool hasPrefix(std::string const& text, char const* prefix)
    {
        return text.rfind(prefix, 0) == 0;
    }

    // 定义在下面（C ABI 类型 → C# 类型）；镜像的 C# 结构体生成要用它，先声明。
    std::string toCSharpType(std::string const& cType);

    // ---- 生成期错误码表（本 change 任务 4.3）----
    // 生成物里每个 `<接口>_ERROR_*` 常量都从这张表发出；`Tools/CodeGen/check_bindings.py` 的
    // `ERROR_CODE_TABLE` 与 `Tools/CodeGen/README.md` 的错误码表必须同步包含同一组名字与数值。
    // 新增错误标识时三处一起改，`check_error_codes()` 会把只改一侧的情况判红。
    struct BindErrorCode
    {
        char const* name;
        int32_t value;
    };

    BindErrorCode const kBindErrorCodes[] = {
        { "STALE_HANDLE", 1001 },
        { "NULL_POINTER", 1002 },
        // 回调桥：用户委托抛异常 / 回调执行期间改同一个注册 / 非注册线程回调（本 change 4.3）
        { "CALLBACK_EXCEPTION", 1003 },
        { "CALLBACK_REENTRANT", 1004 },
        { "CALLBACK_WRONG_THREAD", 1005 },
        // 绑定层校验显示器编号（本 change 3.2）
        { "INVALID_MONITOR", 1006 },
        // 序列化块（本 change 任务 3.2）：块头三项与载荷分别有独立错误码，便于测试逐项点名。
        { "BAD_BLOCK_VERSION", 1007 },
        { "BAD_BLOCK_TYPE", 1008 },
        { "BAD_BLOCK_LAYOUT", 1009 },
        { "BAD_BLOCK_PAYLOAD", 1010 },
        // D11：序列化块里的对象句柄（值类容器元素 / 接口对象引用）在绑定对象表里查不到。
        { "BAD_OBJECT_HANDLE", 1011 },
        // 反向接口（csharp-reverse-interface-binding）：C# 覆写抛异常 / 反向实现返回失败 / 不支持的
        // 反向读（Deserialize 本批显式排除）。三处（这里 / check_bindings.py 的 ERROR_CODE_TABLE /
        // README 的错误码表）必须同步。
        { "REVERSE_EXCEPTION", 1012 },
        { "REVERSE_READ_UNSUPPORTED", 1013 },
    };

    // 反向专属错误码（1012/1013）只发射进**反向**产物：正向产物（含既有 8 接口、夹具、窗口/渲染）
    // 必须逐字节不变 —— tasks 8.3 的基线判据是"既有 8 接口正向产物逐份哈希相同"，而正向路径上
    // 永远不会出现这两个码。错误码表的**全局**一致性由 README 与 `check_bindings.ERROR_CODE_TABLE`
    // 保证（两处都含 1012/1013），不是靠每份产物都抄一遍。
    bool isReverseOnlyErrorCode(BindErrorCode const& errorCode)
    {
        return errorCode.value == 1012 || errorCode.value == 1013;
    }

    // 回调形状要认"参数是不是已绑定接口的裸指针"，所以这两个声明提前到这里。
    struct MethodSignature
    {
        std::string name;
        std::string returnSpelling;
        std::string returnCanonicalSpelling;   // 别名解析后的拼写（EnumFlags 等模板要按它认）
        int returnKind = 0;                                        // CXTypeKind 的取值，用来认枚举
        std::vector<std::pair<std::string, std::string>> params;   // （类型拼写，形参名）
        std::vector<std::string> paramCanonicalSpellings;          // 与 params 同序
        std::vector<int> paramKinds;
        bool isConst = false;
        // 本批（codegen-native-binding-prereqs 2.2–2.4）新增的采集元数据。
        bool isPure = false;           // 纯虚（现行审计表的口径仍以纯虚为主）
        bool isVirtual = false;        // virtual（含非纯虚）—— 反向投影只绑可覆写的虚方法
        bool isStatic = false;         // 静态成员函数（任务 2.3 只接"返回纯标量记录的静态工厂"）
        bool isTemplate = false;       // 成员函数模板（① 类：显式排除或生成期拒绝）
        bool returnsSelfRef = false;   // 返回本类引用（任务 2.2：C ABI 投影成 void，C# 返回 this）
        // C ABI 槽名：同名重载第 1 槽保持原名，第 2 槽起 `_<序数>`（任务 2.4）；
        // 2.2 过渡期同名第二槽留空 = 不绑（由 CA_BIND_SKIP 之外的临时口径处理）。
        std::string abiName;
        // 任务 3.3：该方法的注入参数名（从 CA_BIND_INJECT 读入，C ABI/托管表面都不出现）。
        std::set<std::string> injectedParams;
        // `CA_BIND_SKIP` 带理由的方法（本批 2.2）：打一次标记，各道统一按它跳过。
        bool skipped = false;
        // 内存窗配对（csharp-native-direct-binding 任务 1.1 / design D2）：指针参数下标 ↔ 长度参数
        // 下标。run() 的配对检查填这两张表；配对失败在那一轮就非零点名，不留给投影去猜。
        std::map<std::size_t, std::size_t> windowLengthByPointer;
        std::map<std::size_t, std::size_t> windowPointerByLength;
        // 模板显式实例化声明（任务 1.3 / design D9）：`CA_BIND_INSTANTIATE(方法, 类型...)` 的
        // 类型列表。只对模板成员函数有意义；未声明的模板形态不进任何表面。
        std::vector<std::string> instantiateTypes;
        // 模板成员函数的模板参数名（通常就一个 `T`）：实例化时按名替换成具体类型。
        std::vector<std::string> templateParams;
        std::string ownerName;   // 所属接口短名（钩子命名要用）
        std::string ownerQualified;   // 所属接口完整限定名（D2：钩子名用限定名线形）
        // 返回位置标量容器两段式（本批 1.6 / design D13）：元素宽度类型（`uint32_t` 之类）。
        // 非空 = 这条方法按 `Count` + `Copy` 两个伴生出绑定，托管表面得到元素数组。
        std::string twoStepScalarElement;
    };

    // 已标记 CA_BIND 的接口名（`run` 里填）。`projectMethod` 与回调形状解析都要用它判
    // "这个裸指针指向的是不是已绑定接口"。
    std::set<std::string> g_bindableInterfaceNames;

    // ---- 内存窗配对（csharp-native-direct-binding 任务 1.1 / design D2）----
    // 固定形状：`const void*` 参数 + 同参数表内**相邻唯一**的整型长度参数（常见 uint64_t）。
    // 配对判据=相邻（前一或后一个形参）的整型参数恰好一个：
    //   · 0 个 ⇒ 长度缺失，生成期点名（不许把裸指针暴露给托管）；
    //   · 2 个（两侧都是整型）⇒ 配对不唯一，生成期点名（不许猜哪个是长度）。
    // 投影：托管收 `byte[]`；C ABI 两段 `uint8_t const* <名>` + `uint64_t <名>Count`。
    bool isConstVoidPointerSpelling(std::string const& spelling)
    {
        std::string text = trimCopy(spelling);
        if (text.empty() || text.back() != '*')
        {
            return false;
        }
        if (text.find("const") == std::string::npos)
        {
            return false;   // 非 const 的 `void*` 是出参方向，不是内存窗
        }
        text.pop_back();
        text = trimCopy(text);
        if (text.rfind("const ", 0) == 0)
        {
            text = trimCopy(text.substr(6));
        }
        if (text.size() > 6 && text.compare(text.size() - 6, 6, " const") == 0)
        {
            text = trimCopy(text.substr(0, text.size() - 6));
        }
        return text == "void";
    }

    bool isIntegerSpelling(std::string const& spelling)
    {
        const std::string bare = bareType(spelling);
        if (!isScalarType(bare))
        {
            // 规范类型拼写（libclang 的 canonical type）会把 `uint64_t` 展开成
            // `unsigned long long` / `__int64` 这类原生拼写 —— 快照模式读的就是规范拼写，
            // 不认这些会把它当成"非整型"，内存窗配对当场误判成缺长度（实测）。
            static const char* const canonicalIntegers[] = {
                "long long", "unsigned long long", "__int8", "unsigned __int8", "__int16",
                "unsigned __int16", "__int32", "unsigned __int32", "__int64", "unsigned __int64",
            };
            for (char const* candidate : canonicalIntegers)
            {
                if (bare == candidate)
                {
                    return true;
                }
            }
            return false;
        }
        return bare != "bool" && bare != "float" && bare != "double";
    }

    // 返回 0 = 唯一配对（*lengthIndex 写下标）；1 = 长度缺失；2 = 多个整型候选。
    int analyzeWindowPair(MethodSignature const& method, std::size_t pointerIndex,
                          std::size_t* lengthIndex)
    {
        std::vector<std::size_t> candidates;
        if (pointerIndex > 0 && isIntegerSpelling(method.params[pointerIndex - 1].first))
        {
            candidates.push_back(pointerIndex - 1);
        }
        if (pointerIndex + 1 < method.params.size() &&
            isIntegerSpelling(method.params[pointerIndex + 1].first))
        {
            candidates.push_back(pointerIndex + 1);
        }
        if (candidates.empty())
        {
            return 1;
        }
        if (candidates.size() > 1)
        {
            return 2;
        }
        *lengthIndex = candidates[0];
        return 0;
    }

    // ---- 枚举元数据（csharp-render-binding-foundations 任务 2.2）----
    // 记录字段里的普通枚举按真实底层类型镜像，`EnumFlags<T>` 按 MaskType 镜像；公开 C# 枚举
    // 要保留常量名、实际值与底层类型。这些信息只能从 TU 里的 EnumDecl 读，不能按短名猜。
    struct EnumConstantInfo
    {
        std::string name;
        int64_t value = 0;
    };

    struct EnumInfo
    {
        std::string cppName;        // 完整限定名（ca_bind_fixture::ERenderMode）
        std::string shortName;      // ERenderMode
        std::string underlying;     // uint16_t / int32_t / ...
        std::vector<EnumConstantInfo> constants;
        bool usedAsFlags = false;   // 被 EnumFlags<T> 使用（公开 C# 枚举要带 [Flags]）
    };

    std::map<std::string, EnumInfo> g_enumsByShort;

    // ---- 资源引用值类（codegen-native-binding-prereqs 任务 3.1）----
    // 只识别显式标了 `CA_BIND_VALUE(短名)` 的类：它是值语义资源引用（内部持智能指针 / 名称哈希、
    // 作哈希键、自身不可进反射表），只作为参数过界；自身成员方法 MUST NOT 进表面。
    // 这个结构定义在 projectMethod 之前 —— 投影函数要用它判"这个参数是不是值类"。
    struct ValueTypeInfo
    {
        std::string shortName;
        std::string qualifiedName;
        std::string enumShort;
        std::string objectPointeeShort;       // shared_ptr<T> 的 T 短名
        std::string objectPointeeQualified;   // T 的完整限定名（必须是已绑定接口）
        std::string objectFactoryName;        // 托管工厂名（From<T 去前导 I>）
        unsigned objectKind = 0;              // 值类枚举里"按对象构造"的常量值
        // 值类可能有多条对象构造通道（真实形状：`ImageHandle(shared_ptr<GPUTexture>)` → External、
        // `ImageHandle(shared_ptr<WindowHandle>)` → Backbuffer，design D12）。上面三个单数字段是
        // **首条**通道（既有调用点不变），这里按声明序记全部通道。
        struct ObjectChannel
        {
            std::string pointeeShort;
            std::string pointeeQualified;
            std::string factoryName;
            unsigned kind = 0;
        };
        std::vector<ObjectChannel> objectChannels;
        bool hasNameCtor = false;
        std::string nameFactoryName = "FromName";
        unsigned nameKind = 0;
        // 无效态（枚举里的 `Invalid`）：值类字段的"未设置"要能过界（`IndexBufferData::indexBufferHandle`
        // 在原生 `DrawCall::New()` 里就是默认构造的 Invalid 句柄）；不猜时保持 false。
        bool hasInvalidKind = false;
        unsigned invalidKind = 0;
    };

    std::map<std::string, ValueTypeInfo> g_valueTypes;

    // ---- 序列化块记录（codegen-native-binding-prereqs 任务 3.2）----
    // 参数位置、成员含一层容器或文本的记录：不走镜像快速路径，改走"序列化块"：
    // 块头三项（formatVersion + 类型名哈希 + 字段布局哈希）+ 字段级二进制；C++ 反序列化与
    // C# 序列化从同一份字段清单生成。字段布局哈希把"名+类型+偏移"一起算进去，堵"改名同序"。
    struct SerializedField
    {
        std::string name;
        std::string cppType;         // 原生字段类型拼写（诊断与 C# 类型映射）
        // 定长数组元素个数（kind == 8）：个数是类型的一部分，线形里不带 count。
        std::size_t fixedCount = 0;
        // 值类字段（kind == 9）的短名。
        std::string valueClassShort;
        // 联合体字段（kind == 10）的定长字节数。
        std::size_t blobSize = 0;
        int kind = 0;                // 0 标量 / 1 枚举 / 2 文本 / 3 vector / 4 嵌套记录
        std::string widthType;       // 标量或枚举底层类型：uint32_t / uint16_t / float / double / bool
        std::string enumShort;
        uint64_t offsetBytes = 0;
        bool textIsNameHash = false;
        bool textIsPathHash = false;
        // vector 元素
        int elementKind = 0;         // 0 标量 / 1 枚举 / 2 记录 / 3 文本
        std::string elementWidthType;
        std::string elementEnumShort;
        std::string elementRecordShort;
        std::string elementRecordQualified;
        std::vector<SerializedField> elementFields;
        bool elementTextIsNameHash = false;
        bool elementTextIsPathHash = false;
        // 嵌套记录（D8 扩展：元数据记录 ShaderStructData 里含 `ShaderStructUniforms` 这类子记录）
        std::string nestedRecordShort;
        std::string nestedRecordQualified;
        std::vector<SerializedField> nestedFields;
        // 嵌套记录自己的布局清单（父层按"父字段行 + 嵌套行"拼进布局哈希）。
        std::string nestedLayoutText;
        // ---- D11：三类图形状元素 ----
        // ① 值类容器元素（`vector<CA_BIND_VALUE 值类>`）：elementKind = 4 + 值类短名。
        std::string elementValueClassShort;
        // ② 命名映射成员（键固定 NameHash）：kind = 5 + 值类别与值侧元数据。
        int mapValueKind = 0;        // 0 标量 / 1 枚举 / 2 记录 / 3 值类 / 4 接口引用
        std::string mapValueWidthType;
        std::string mapValueEnumShort;
        std::string mapValueRecordShort;
        std::string mapValueRecordQualified;
        std::vector<SerializedField> mapValueFields;
        std::string mapValueValueClassShort;
        std::string mapValueInterfaceShort;
        std::string mapValueInterfaceQualified;
        // ③ 接口对象引用成员（`shared_ptr<已挂 CA_BIND 的接口>`）：kind = 6。
        std::string interfaceShort;
        std::string interfaceQualified;
        // 包装成员（`cacore::HashObj<T>`，真实形状：`HashObj<ViewRectData> m_ViewPort`）：kind = 7，
        // 线形 = 内层记录的字段逐项（读回时重新包成 HashObj）。
        bool isHashObjWrapper = false;
        bool mapValueIsHashObj = false;
    };

    struct SerializedTypeInfo
    {
        std::string shortName;
        std::string qualifiedName;
        uint64_t typeHash = 0;
        uint64_t layoutHash = 0;
        std::string layoutText;
        std::vector<SerializedField> fields;
    };

    struct SerializedElementInfo
    {
        std::string shortName;
        std::string qualifiedName;
        std::vector<SerializedField> fields;
    };

    std::map<std::string, SerializedTypeInfo> g_serializedTypes;
    std::map<std::string, SerializedElementInfo> g_serializedElementRecords;

    // ---- 记录两段式投影（本批 D8）：返回位置指向"可序列化记录"的非拥有裸指针 ----
    // 记录短名清单由 run() 在投影之前填（判定依据=该记录需要序列化块路径）。
    // 命中的方法投影成 `Len` / `Copy` 两个伴生：原生返回空指针 ⇒ Len 返回 0 ⇒ 托管得到 null。
    std::set<std::string> g_recordReturnTypes;

    // D11：这条字段（含嵌套/元素/映射值）是否需要绑定对象表 —— 值类元素 / 命名映射 / 接口引用。
    // 返回位置的记录含这类字段时生成期拒绝（design D11 的 MUST NOT：返回位置映射与对象引用维持不可绑）。
    bool serializedFieldNeedsBindingTable(SerializedField const& field)
    {
        if (field.kind == 5 || field.kind == 6 || field.kind == 8 || field.kind == 9 ||
            field.kind == 10 || field.elementKind == 4)
        {
            return true;
        }
        for (SerializedField const& sub : field.nestedFields)
        {
            if (serializedFieldNeedsBindingTable(sub))
            {
                return true;
            }
        }
        for (SerializedField const& sub : field.elementFields)
        {
            if (serializedFieldNeedsBindingTable(sub))
            {
                return true;
            }
        }
        for (SerializedField const& sub : field.mapValueFields)
        {
            if (serializedFieldNeedsBindingTable(sub))
            {
                return true;
            }
        }
        return false;
    }

    // 已知接口清单（任务 3.1）：`--known-interface <短名>=<完整限定名>`。
    // 它只用于类型投影与诊断，不决定产物 —— 产物仍只看主头里的 CA_BIND。
    // 跨命名空间同短名（本批 1.1）起改成列表：同一个短名可以对应多条完整限定名。
    std::vector<std::pair<std::string, std::string>> g_knownInterfaces;

    bool isEnumUnderlyingSpelling(std::string const& bare)
    {
        return isScalarType(bare) && bare != "bool" && bare != "float" && bare != "double";
    }

    // 解析 `uenum::EnumFlags<T>` / `ca_bind_fixture::ERenderFlags` 这类写法。返回 false 表示不是。
    bool parseFlagsWrapper(std::string const& spelling, std::string* enumShort)
    {
        std::string text = trimCopy(spelling);
        // 去掉 const / 引用 / 指针
        text = bareType(text);
        const std::size_t flagsAt = text.find("EnumFlags");
        if (flagsAt == std::string::npos)
        {
            return false;
        }
        const std::size_t open = text.find('<', flagsAt);
        const std::size_t close = text.rfind('>');
        if (open == std::string::npos || close == std::string::npos || close <= open)
        {
            return false;
        }
        const std::string inner = trimCopy(text.substr(open + 1, close - open - 1));
        const std::size_t split = inner.rfind("::");
        *enumShort = split == std::string::npos ? inner : inner.substr(split + 2);
        return !enumShort->empty();
    }

    bool isNameHashSpelling(std::string const& spelling)
    {
        const std::string bare = bareType(spelling);
        return bare == "cacore::NameHash" || bare == "NameHash";
    }

    bool isPathHashSpelling(std::string const& spelling)
    {
        const std::string bare = bareType(spelling);
        return bare == "cacore::PathHash" || bare == "PathHash";
    }

    bool parseHashObjWrapper(std::string const& spelling, std::string* innerSpelling)
    {
        const std::string bare = bareType(spelling);
        if (bare.rfind("cacore::HashObj<", 0) != 0)
        {
            return false;
        }
        const std::size_t open = bare.find('<');
        const std::size_t close = bare.rfind('>');
        if (open == std::string::npos || close == std::string::npos || close <= open)
        {
            return false;
        }
        std::string inner = trimCopy(bare.substr(open + 1, close - open - 1));
        // 模板实参可能是 `T, 模式, 算法`：只取第一个类型（按顶层逗号切）。
        int depth = 0;
        std::string first;
        for (char character : inner)
        {
            if (character == '<' || character == '(') { ++depth; }
            else if (character == '>' || character == ')') { --depth; }
            if (character == ',' && depth == 0) { break; }
            first.push_back(character);
        }
        *innerSpelling = trimCopy(first);
        return !innerSpelling->empty();
    }

    bool isByteVectorSpelling(std::string const& spelling)
    {
        const std::string bare = bareType(spelling);
        if (bare.rfind("castl::vector<", 0) != 0 && bare.rfind("std::vector<", 0) != 0)
        {
            return false;
        }
        const std::size_t open = bare.find('<');
        const std::size_t close = bare.rfind('>');
        if (open == std::string::npos || close == std::string::npos || close <= open)
        {
            return false;
        }
        const std::string element = bareType(trimCopy(bare.substr(open + 1, close - open - 1)));
        return element == "uint8_t";
    }

    std::string cSharpIntegerType(std::string const& underlying)
    {
        if (underlying == "uint8_t" || underlying == "unsigned char" || underlying == "char") { return "byte"; }
        if (underlying == "int8_t" || underlying == "signed char") { return "sbyte"; }
        if (underlying == "uint16_t" || underlying == "unsigned short") { return "ushort"; }
        if (underlying == "int16_t" || underlying == "short") { return "short"; }
        if (underlying == "uint32_t" || underlying == "unsigned int" || underlying == "unsigned") { return "uint"; }
        if (underlying == "int32_t" || underlying == "int") { return "int"; }
        if (underlying == "uint64_t" || underlying == "unsigned long" || underlying == "size_t") { return "ulong"; }
        if (underlying == "int64_t" || underlying == "long") { return "long"; }
        return std::string();
    }


    // ---- 回调形状（`castl::function<void(Args...)>`，本 change 任务 2.1）----
    struct CallbackArg
    {
        std::string cppType;          // 原始 C++ 类型拼写（lambda 形参照抄，顺序与声明一致）
        std::string cAbiType;         // 原生函数指针里的类型（`bool` 按 `int32_t`，接口指针按 `void*`）
        std::string surfaceType;      // 托管 `Action<...>` 的泛型参数
        std::string interfaceName;    // 非空 = 已绑定接口的裸指针（回调期间借用的表面对象）
    };

    struct CallbackShape
    {
        bool valid = false;
        std::string whyNot;
        std::vector<CallbackArg> args;
    };

    std::vector<std::string> splitTopLevelArguments(std::string const& text)
    {
        std::vector<std::string> parts;
        int depth = 0;
        std::string current;
        for (char character : text)
        {
            if (character == '<' || character == '(' || character == '[')
            {
                ++depth;
            }
            else if (character == '>' || character == ')' || character == ']')
            {
                --depth;
            }
            if (character == ',' && depth == 0)
            {
                parts.push_back(trimCopy(current));
                current.clear();
                continue;
            }
            current.push_back(character);
        }
        if (!trimCopy(current).empty())
        {
            parts.push_back(trimCopy(current));
        }
        return parts;
    }

    // 标量在托管 `Action<...>` 里的类型；认不出来的返回空串（调用方按"不可投影"拒绝，不静默降级）。
    std::string callbackSurfaceScalar(std::string const& bare)
    {
        if (bare == "void") { return "void"; }
        if (bare == "bool") { return "bool"; }
        if (bare == "int" || bare == "int32_t") { return "int"; }
        if (bare == "uint32_t" || bare == "unsigned" || bare == "unsigned int") { return "uint"; }
        if (bare == "int64_t" || bare == "long") { return "long"; }
        if (bare == "uint64_t" || bare == "unsigned long") { return "ulong"; }
        if (bare == "uint8_t" || bare == "char") { return "byte"; }
        if (bare == "float") { return "float"; }
        if (bare == "double") { return "double"; }
        if (bare == "size_t") { return "ulong"; }
        return std::string();
    }

    // `castl::function<void(Args...)>` → 参数表。只支持标量与"已绑定接口的裸指针"；其余一律拒绝并说明。
    CallbackShape parseCallbackShape(std::string const& spelling)
    {
        CallbackShape shape;
        const std::string bare = bareType(spelling);
        if (!hasPrefix(bare, "castl::function") && !hasPrefix(bare, "std::function"))
        {
            shape.whyNot = "不是 castl::function 类型（" + spelling + "）";
            return shape;
        }
        const std::size_t less = bare.find('<');
        const std::size_t greater = bare.rfind('>');
        if (less == std::string::npos || greater == std::string::npos || greater <= less)
        {
            shape.whyNot = "回调类型拼写不完整（" + spelling + "）";
            return shape;
        }
        const std::string inside = trimCopy(bare.substr(less + 1, greater - less - 1));
        const std::size_t open = inside.find('(');
        if (open == std::string::npos)
        {
            shape.whyNot = "回调类型里找不到参数表（" + spelling + "）";
            return shape;
        }
        const std::string returnSpelling = trimCopy(inside.substr(0, open));
        if (returnSpelling != "void")
        {
            shape.whyNot = "回调只支持 void 返回（实际 " + returnSpelling + "）";
            return shape;
        }
        int depth = 0;
        std::size_t close = std::string::npos;
        for (std::size_t index = open; index < inside.size(); ++index)
        {
            if (inside[index] == '(') { ++depth; }
            else if (inside[index] == ')')
            {
                --depth;
                if (depth == 0) { close = index; break; }
            }
        }
        if (close == std::string::npos)
        {
            shape.whyNot = "回调参数表的右括号不配对（" + spelling + "）";
            return shape;
        }
        const std::string argumentText = inside.substr(open + 1, close - open - 1);
        for (std::string const& argument : splitTopLevelArguments(argumentText))
        {
            CallbackArg parsed;
            parsed.cppType = trimCopy(argument);
            const std::string argumentBare = bareType(argument);
            if (isScalarType(argumentBare))
            {
                parsed.cAbiType = argumentBare == "bool" ? "int32_t" : argumentBare;
                parsed.surfaceType = callbackSurfaceScalar(argumentBare);
                if (parsed.surfaceType.empty())
                {
                    shape.whyNot = "回调参数 " + argument + " 没有对应的托管标量类型";
                    return shape;
                }
                shape.args.push_back(std::move(parsed));
                continue;
            }
            if (argument.find('*') != std::string::npos)
            {
                const std::string pointee = unqualifiedName(argumentBare);
                if (g_bindableInterfaceNames.count(pointee) == 0)
                {
                    shape.whyNot = "回调参数 " + argument + " 是裸指针，但它指向的不是已绑定接口";
                    return shape;
                }
                parsed.cAbiType = "void*";
                parsed.surfaceType = pointee;
                parsed.interfaceName = pointee;
                shape.args.push_back(std::move(parsed));
                continue;
            }
            shape.whyNot = "回调参数 " + argument + " 不是标量、也不是已绑定接口的裸指针";
            return shape;
        }
        shape.valid = true;
        return shape;
    }

    // `castl::function<void(Args...)>` → `void(Args...)`（构造与清空原生回调时要用它再包回去）。
    // 参数里的接口名是按接口所在命名空间给的短名（`IWindow *`），生成物在全局作用域，必须补限定名。
    std::string callbackCppFunctionType(CallbackShape const& shape, std::string const& interfaceNamespace)
    {
        std::string type = "void (";
        for (std::size_t index = 0; index < shape.args.size(); ++index)
        {
            const CallbackArg& argument = shape.args[index];
            const std::string bare = bareType(argument.cppType);
            std::string qualified = argument.cppType;
            if (!isScalarType(bare) && qualified.find("::") == std::string::npos &&
                !interfaceNamespace.empty())
            {
                qualified = interfaceNamespace + qualified;
            }
            type += (index == 0 ? "" : ", ") + qualified;
        }
        type += ")";
        return type;
    }

    // 原生函数指针类型（不带形参名）：`void (*)(void* user, <args...>)`。
    std::string callbackFunctionPointerType(CallbackShape const& shape)
    {
        std::string type = "void (*)(void*";
        for (CallbackArg const& argument : shape.args)
        {
            type += ", " + argument.cAbiType;
        }
        type += ")";
        return type;
    }

    // 函数指针类型的 C# 写法：`delegate* unmanaged<void*, int32_t, …, void>`。
    std::string callbackCSharpFunctionPointer(CallbackShape const& shape)
    {
        std::string type = "delegate* unmanaged<void*";
        for (CallbackArg const& argument : shape.args)
        {
            if (argument.cAbiType == "int32_t" || argument.cAbiType == "int")
            {
                type += ", int";
            }
            else if (argument.cAbiType == "uint32_t")
            {
                type += ", uint";
            }
            else if (argument.cAbiType == "float")
            {
                type += ", float";
            }
            else if (argument.cAbiType == "double")
            {
                type += ", double";
            }
            else
            {
                // 接口指针（`void*`）与其它 64 位令牌都按指针宽度走
                type += ", void*";
            }
        }
        type += ", void>";
        return type;
    }

    // 一条方法里的回调参数（目前只允许"唯一形参就是回调"这一种形状；IWindowSystem 的 10 个 setter 都是这种）。
    struct MethodCallbackInfo
    {
        std::size_t paramIndex = 0;
        std::string paramName;
        CallbackShape shape;
    };

    bool analyzeCallbackMethod(MethodSignature const& method, MethodCallbackInfo* info, std::string* whyNot)
    {
        bool found = false;
        for (std::size_t index = 0; index < method.params.size(); ++index)
        {
            const std::string bare = bareType(method.params[index].first);
            if (!hasPrefix(bare, "castl::function") && !hasPrefix(bare, "std::function"))
            {
                continue;
            }
            if (found)
            {
                *whyNot = "一条方法里只允许一个回调参数";
                return false;
            }
            if (method.params.size() != 1)
            {
                *whyNot = "回调参数必须是方法的唯一形参（实际还有其它形参）";
                return false;
            }
            info->paramIndex = index;
            info->paramName = method.params[index].second.empty() ? std::string("callback")
                                                                  : method.params[index].second;
            info->shape = parseCallbackShape(method.params[index].first);
            if (!info->shape.valid)
            {
                *whyNot = info->shape.whyNot;
                return false;
            }
            found = true;
        }
        return found;
    }

    struct Projected
    {
        std::string type;
        std::string name;
        bool isCallbackFn = false;
        CallbackShape callback;
    };

    // 形参的完整声明：普通类型是 `T name`，函数指针要把名字插进 `(*)` 里。
    std::string projectedDeclaration(Projected const& projected)
    {
        if (projected.isCallbackFn)
        {
            const std::size_t marker = projected.type.find("(*)");
            if (marker != std::string::npos)
            {
                // `void (*)(...)` → `void (*name)(...)`：名字插在 `(*)` 的两个括号之间
                return projected.type.substr(0, marker + 2) + projected.name + projected.type.substr(marker + 2);
            }
        }
        return projected.type + " " + projected.name;
    }

    struct InterfaceShape
    {
        std::string name;
        std::string qualifiedName; // 带命名空间的完整拼写，用于 static_cast
        std::vector<MethodSignature> methods;
        // 任务 2.3：带公开默认构造的非抽象类生成 `<类>_Create` 入口。
        bool hasPublicDefaultCtor = false;
        bool isAbstract = false;
        // csharp-reverse-interface-binding：这个接口是**反向**的（`CA_BIND_REVERSE()`，C# 实现 C++）。
        // 反向接口走独立的发射分支，不与正向产物（IDisposable 表面类 + Api_v2 函数表）混在一起。
        bool isReverse = false;
    };

    struct ClassCollector
    {
        std::vector<std::string> const* wanted = nullptr;
        std::vector<InterfaceShape>* sink = nullptr;
    };

    struct CtorProbe
    {
        bool hasPublicDefaultCtor = false;
        bool hasPureVirtual = false;
        // 任意用户声明的构造函数（含拷贝/移动）。为假且非抽象 ⇒ 默认构造是**隐式公开**的。
        bool hasAnyCtor = false;
    };

    CXChildVisitResult collectCtorProbe(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* probe = static_cast<CtorProbe*>(data);
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_CXXMethod && clang_CXXMethod_isPureVirtual(cursor) != 0u)
        {
            probe->hasPureVirtual = true;
        }
        if (kind == CXCursor_Constructor)
        {
            probe->hasAnyCtor = true;
        }
        if (kind == CXCursor_Constructor &&
            clang_CXXConstructor_isDefaultConstructor(cursor) != 0u &&
            clang_getCXXAccessSpecifier(cursor) == CX_CXXPublic &&
            clang_CXXMethod_isDeleted(cursor) == 0u)
        {
            probe->hasPublicDefaultCtor = true;
        }
        return CXChildVisit_Continue;
    }

    struct MethodCollector
    {
        std::vector<MethodSignature>* sink = nullptr;
    };

    bool hasMethodNamed(std::vector<MethodSignature> const& methods, MethodSignature const& candidate)
    {
        for (MethodSignature const& method : methods)
        {
            if (method.name != candidate.name || method.params.size() != candidate.params.size())
            {
                continue;
            }
            bool sameTypes = true;
            for (std::size_t i = 0; i < method.params.size(); ++i)
            {
                if (method.params[i].first != candidate.params[i].first)
                {
                    sameTypes = false;
                    break;
                }
            }
            if (sameTypes)
            {
                return true;
            }
        }
        return false;
    }
    CXChildVisitResult collectMethods(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* collector = static_cast<MethodCollector*>(data);
        const CXCursorKind kind = clang_getCursorKind(cursor);
        // 本批（任务 2.2）：公开成员函数**不再限定纯虚** —— 非纯虚 virtual 与非虚公开方法都要进表。
        // 仍然不采的三类：非 public（父类 private/protected）、静态成员（任务 2.3 才接）、
        // 成员函数模板（libclang 报 FunctionTemplate；① 类，由 CA_BIND_SKIP 排除或生成期拒绝）。
        const bool isTemplate = kind == CXCursor_FunctionTemplate;
        if (!isTemplate && kind != CXCursor_CXXMethod)
        {
            return CXChildVisit_Continue;
        }
        const CX_CXXAccessSpecifier access = clang_getCXXAccessSpecifier(cursor);
        // 结构体的默认访问级是 public，libclang 对"没写访问标号"的成员给 Invalid —— 放行，
        // 由下面的类种类判定并不参与（这里只挡显式 private/protected）。
        if (access == CX_CXXPrivate || access == CX_CXXProtected)
        {
            return CXChildVisit_Continue;
        }
        MethodSignature method;
        method.name = toStd(clang_getCursorSpelling(cursor));
        if (method.name.empty() || method.name[0] == '~')
        {
            return CXChildVisit_Continue;
        }
        method.isTemplate = isTemplate;
        if (isTemplate)
        {
            // FunctionTemplate 游标上取不到"函数类型"（Clang 报 Invalid），但可以从子游标读到
            // 模板参数名与形参声明 —— 显式实例化声明（D9）要按名替换成具体类型。
            if (hasMethodNamed(*collector->sink, method))
            {
                return CXChildVisit_Continue;
            }
            struct TemplateShape
            {
                MethodSignature* method = nullptr;
            } templateShape{ &method };
            clang_visitChildren(
                cursor,
                [](CXCursor child, CXCursor, CXClientData data) {
                    auto* shape = static_cast<TemplateShape*>(data);
                    const CXCursorKind childKind = clang_getCursorKind(child);
                    if (childKind == CXCursor_TemplateTypeParameter)
                    {
                        shape->method->templateParams.push_back(toStd(clang_getCursorSpelling(child)));
                    }
                    else if (childKind == CXCursor_ParmDecl)
                    {
                        const CXType childType = clang_getCursorType(child);
                        shape->method->params.emplace_back(toStd(clang_getTypeSpelling(childType)),
                                                           toStd(clang_getCursorSpelling(child)));
                        shape->method->paramCanonicalSpellings.push_back(
                            toStd(clang_getTypeSpelling(clang_getCanonicalType(childType))));
                        shape->method->paramKinds.push_back(
                            static_cast<int>(clang_getCanonicalType(childType).kind));
                    }
                    return CXChildVisit_Continue;
                },
                &templateShape);
            const CXType templateResult = clang_getCursorResultType(cursor);
            if (templateResult.kind != CXType_Invalid)
            {
                method.returnSpelling = toStd(clang_getTypeSpelling(templateResult));
                method.returnCanonicalSpelling =
                    toStd(clang_getTypeSpelling(clang_getCanonicalType(templateResult)));
                method.returnKind = static_cast<int>(clang_getCanonicalType(templateResult).kind);
            }
            collector->sink->push_back(std::move(method));
            return CXChildVisit_Continue;
        }
        method.isPure = clang_CXXMethod_isPureVirtual(cursor) != 0u;
        method.isVirtual = clang_CXXMethod_isVirtual(cursor) != 0u;
        method.isStatic = clang_CXXMethod_isStatic(cursor) != 0u;
        method.isConst = clang_CXXMethod_isConst(cursor) != 0u;

        const CXType functionType = clang_getCursorType(cursor);
        const CXType resultType = clang_getResultType(functionType);
        method.returnSpelling = toStd(clang_getTypeSpelling(resultType));
        method.returnCanonicalSpelling = toStd(clang_getTypeSpelling(clang_getCanonicalType(resultType)));
        method.returnKind = static_cast<int>(clang_getCanonicalType(resultType).kind);

        const int count = clang_Cursor_getNumArguments(cursor);
        for (int index = 0; index < count; ++index)
        {
            const CXCursor argument = clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
            const CXType argumentCXType = clang_getCursorType(argument);
            const std::string argumentType = toStd(clang_getTypeSpelling(argumentCXType));
            const std::string argumentName = toStd(clang_getCursorSpelling(argument));
            method.params.emplace_back(argumentType, argumentName);
            method.paramCanonicalSpellings.push_back(
                toStd(clang_getTypeSpelling(clang_getCanonicalType(argumentCXType))));
            method.paramKinds.push_back(static_cast<int>(clang_getCanonicalType(argumentCXType).kind));
        }
        // 前向声明与定义会被访问两次：同名同参的方法只收一次（此时形参已填好）
        if (hasMethodNamed(*collector->sink, method))
        {
            return CXChildVisit_Continue;
        }
        collector->sink->push_back(std::move(method));
        return CXChildVisit_Continue;
    }


    CXChildVisitResult collectClasses(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* collector = static_cast<ClassCollector*>(data);
        if (clang_getCursorKind(cursor) == CXCursor_ClassDecl || clang_getCursorKind(cursor) == CXCursor_StructDecl)
        {
            // 前向声明也会走这个 visitor：它不是定义、拿不到方法，先把它收下会让后面真正的定义
            // 被下面的"只收一次"挡掉。实测：`IWindowSystem` 前面有一句 `class IWindowSystem;`
            // 前向声明，结果 `[接口] IWindowSystem → 0 条`、产物一个都没出。
            if (clang_isCursorDefinition(cursor) == 0)
            {
                return CXChildVisit_Recurse;
            }
            const std::string name = toStd(clang_getCursorSpelling(cursor));
            // 带命名空间的完整拼写（thunk 里 static_cast 要用它；只写短名在命名空间里编不过）。
            // libclang 的类型拼写在这里只给短名，所以自己沿"语义父"把命名空间串起来。
            std::string qualified;
            {
                std::string prefix;
                for (CXCursor parent = clang_getCursorSemanticParent(cursor);
                     !clang_Cursor_isNull(parent) &&
                     clang_getCursorKind(parent) == CXCursor_Namespace;
                     parent = clang_getCursorSemanticParent(parent))
                {
                    prefix = toStd(clang_getCursorSpelling(parent)) + "::" + prefix;
                }
                qualified = prefix + name;
            }
            for (std::string const& wanted : *collector->wanted)
            {
                if (name == wanted)
                {
                    // 同一**完整限定名**可能被访问多次（前向声明 + 定义）：只收一次。
                    // 本批 1.1：同短名不同命名空间是两个不同的接口，都必须收下 —— 否则映射后
                    // 碰撞夹具永远看不到冲突的一方，判据恒绿。
                    bool alreadyCollected = false;
                    for (InterfaceShape const& existing : *collector->sink)
                    {
                        if (existing.qualifiedName == qualified)
                        {
                            alreadyCollected = true;
                            break;
                        }
                    }
                    if (alreadyCollected)
                    {
                        return CXChildVisit_Recurse;
                    }
                    InterfaceShape shape;
                    shape.name = name;
                    shape.qualifiedName = qualified;
                    MethodCollector methods;
                    methods.sink = &shape.methods;
                    clang_visitChildren(cursor, collectMethods, &methods);
                    CtorProbe probe;
                    clang_visitChildren(cursor, collectCtorProbe, &probe);
                    shape.hasPublicDefaultCtor = probe.hasPublicDefaultCtor;
                    // 隐式公开默认构造（真实形状：`GPUGraph` 的 public 段没有任何构造函数声明）：
                    // 语言规定它存在且公开，但 libclang **不会为隐式构造生成 Constructor 游标** ——
                    // 只看游标会漏掉 `<类>_Create` 槽（实测 3.1 对账把 `CreateGraph` 判成"无等价物"）。
                    if (!probe.hasPublicDefaultCtor && !probe.hasAnyCtor)
                    {
                        shape.hasPublicDefaultCtor = true;
                    }
                    shape.isAbstract = probe.hasPureVirtual;
                    collector->sink->push_back(std::move(shape));
                    break;
                }
            }
            return CXChildVisit_Recurse;
        }
        return CXChildVisit_Recurse;
    }

    // 从源码文本里读 `CA_BIND`（与之后的 class/struct 名字）。与 CA_REFLECT 同一条通道：按文本认。
    std::string readWholeFile(const char* path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            return {};
        }
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // 从源码文本里读 `CA_BIND`（紧跟着的 class/struct 名字）与 `CA_BIND_SKIP(方法, "理由")`。
    struct BoundMarker
    {
        std::string interfaceName;
        bool isTemplate = false;
        // csharp-reverse-interface-binding：`CA_BIND_REVERSE()` 标的是**反向接口**（C# 实现 C++）。
        // 正向产物（表面类 + Api_v2）不生成；改走反向代理/抽象基类/glue 三件套。
        bool reverse = false;
        // 历史编号参数：括号里写了数字时记下 hasNumber/number，供入口统一拒绝（任务 1.4）——
        // 编号唯一来源是锁文件，无参 `CA_BIND()` 是唯一合法标注。
        bool hasNumber = false;
        unsigned number = 0;
        std::string numberText;   // 宏里原样的参数文本，报错时用（例如 "8" / "" / "abc"）
        std::map<std::string, std::string> skipReasons;  // 方法 → 理由（空串 = 没写理由）
        // 方法 → 注入参数名列表（任务 3.3：`CA_BIND_INJECT(方法, 参数)`）。
        std::map<std::string, std::vector<std::string>> injectParams;
        // 方法 → 显式实例化类型列表（本批 D9：`CA_BIND_INSTANTIATE(方法, 类型...)`）。
        std::map<std::string, std::vector<std::string>> instantiateTypes;
    };

    std::vector<BoundMarker> scanBoundMarkers(std::string const& text)
    {
        // 先剥注释再扫：`CA_BIND` / `CA_BIND_SKIP` 是按**文本**扫的，注释里的也算 ——
        // 于是"把标记注释掉"在它眼里等于没注释（实测：写成 `// PROBE-21 CA_BIND()` 后
        // 生成器照样产出该接口的产物，想用"去掉标记"造红态永远造不出来）。
        std::string stripped;
        stripped.reserve(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            const char current = text[index];
            if (current == '/' && index + 1 < text.size() && text[index + 1] == '/')
            {
                while (index < text.size() && text[index] != '\n')
                {
                    ++index;
                }
                if (index < text.size())
                {
                    stripped.push_back('\n');
                }
                continue;
            }
            if (current == '/' && index + 1 < text.size() && text[index + 1] == '*')
            {
                index += 2;
                while (index + 1 < text.size() && !(text[index] == '*' && text[index + 1] == '/'))
                {
                    ++index;
                }
                index += 1;
                stripped.push_back('\n');
                continue;
            }
            stripped.push_back(current);
        }
        const std::string& source = stripped;
        std::vector<BoundMarker> markers;
        std::size_t position = 0;
        bool pendingBind = false;
        bool pendingReverse = false;
        bool pendingTemplate = false;
        bool pendingHasNumber = false;
        unsigned pendingNumber = 0;
        std::string pendingNumberText;
        while (position < source.size())
        {
            const std::size_t lineEnd = source.find('\n', position);
            std::string line = source.substr(position, lineEnd == std::string::npos ? std::string::npos
                                                                                   : lineEnd - position);
            position = lineEnd == std::string::npos ? source.size() : lineEnd + 1;

            if (line.find("template") != std::string::npos)
            {
                pendingTemplate = true;
            }
            // `CA_BIND_REVERSE()`（csharp-reverse-interface-binding）：反向接口，标注无参数。
            // 必须先于 `CA_BIND(` 判：两者是不同标注（文本上 `CA_BIND_REVERSE(` 不含 `CA_BIND(`）。
            if (line.find("CA_BIND_REVERSE(") != std::string::npos)
            {
                pendingBind = true;
                pendingReverse = true;
                // 带编号标注一律拒绝（编号唯一来源是锁文件）：和 `CA_BIND(...)` 同口径把括号里的文本
                // 记下来交给入口统一点名 —— 否则 `CA_BIND_REVERSE(9)` 会被静默当成合法标注。
                const std::size_t open = line.find('(', line.find("CA_BIND_REVERSE("));
                const std::size_t close = open == std::string::npos ? std::string::npos
                                                                    : line.find(')', open);
                pendingNumberText = (open == std::string::npos || close == std::string::npos)
                                        ? std::string()
                                        : trimCopy(line.substr(open + 1, close - open - 1));
                pendingHasNumber = false;
                pendingNumber = 0;
                if (!pendingNumberText.empty())
                {
                    bool allDigits = true;
                    for (char ch : pendingNumberText)
                    {
                        if (!std::isdigit(static_cast<unsigned char>(ch)))
                        {
                            allDigits = false;
                            break;
                        }
                    }
                    if (allDigits)
                    {
                        pendingHasNumber = true;
                        pendingNumber = static_cast<unsigned>(std::strtoul(pendingNumberText.c_str(), nullptr, 10));
                    }
                }
                continue;
            }
            if (line.find("CA_BIND(") != std::string::npos)
            {
                pendingBind = true;
                // 历史编号参数：括号里是纯十进制数字时记下；其余写法原样进 numberText，由入口点名报错。
                const std::size_t open = line.find('(', line.find("CA_BIND("));
                const std::size_t close = open == std::string::npos ? std::string::npos
                                                                    : line.find(')', open);
                pendingNumberText = (open == std::string::npos || close == std::string::npos)
                                        ? std::string()
                                        : trimCopy(line.substr(open + 1, close - open - 1));
                pendingHasNumber = false;
                pendingNumber = 0;
                if (!pendingNumberText.empty())
                {
                    bool allDigits = true;
                    for (char ch : pendingNumberText)
                    {
                        if (!std::isdigit(static_cast<unsigned char>(ch)))
                        {
                            allDigits = false;
                            break;
                        }
                    }
                    if (allDigits)
                    {
                        pendingHasNumber = true;
                        pendingNumber = static_cast<unsigned>(std::strtoul(pendingNumberText.c_str(), nullptr, 10));
                    }
                }
                continue;
            }
            // 排除：紧跟在本接口之后、下一个 CA_BIND 之前出现
            const std::size_t skipAt = line.find("CA_BIND_SKIP(");
            if (skipAt != std::string::npos && !markers.empty())
            {
                const std::size_t open = line.find('(', skipAt);
                const std::size_t close = line.find(')', open);
                const std::string inside =
                    (open != std::string::npos && close != std::string::npos)
                        ? line.substr(open + 1, close - open - 1)
                        : std::string();
                const std::size_t comma = inside.find(',');
                const std::string method = trimCopy(comma == std::string::npos ? inside
                                                                              : inside.substr(0, comma));
                std::string reason = trimCopy(comma == std::string::npos ? std::string()
                                                                         : inside.substr(comma + 1));
                if (reason == "\"\"" || reason == "\"," || reason.empty())
                {
                    reason.clear(); // 没写理由，后面按错误报
                }
                markers.back().skipReasons[method] = reason;
                continue;
            }
            // `CA_BIND_INJECT(方法, 参数)`（任务 3.3）：参数位置裸指针声明为引擎默认提供。
            if (line.find("CA_BIND_INJECT(") != std::string::npos && !markers.empty())
            {
                const std::size_t injectAt = line.find("CA_BIND_INJECT(");
                const std::size_t open = line.find('(', injectAt);
                const std::size_t close = line.find(')', open);
                const std::string inside =
                    (open != std::string::npos && close != std::string::npos)
                        ? line.substr(open + 1, close - open - 1)
                        : std::string();
                const std::size_t comma = inside.find(',');
                const std::string method =
                    trimCopy(comma == std::string::npos ? inside : inside.substr(0, comma));
                const std::string parameter =
                    trimCopy(comma == std::string::npos ? std::string() : inside.substr(comma + 1));
                if (!method.empty())
                {
                    markers.back().injectParams[method].push_back(parameter);
                }
                continue;
            }
            // `CA_BIND_INSTANTIATE(方法, 类型...)`（本批 D9）：模板方法要绑的具体形态。
            if (line.find("CA_BIND_INSTANTIATE(") != std::string::npos && !markers.empty())
            {
                const std::size_t at = line.find("CA_BIND_INSTANTIATE(");
                const std::size_t open = line.find('(', at);
                const std::size_t close = line.find(')', open);
                const std::string inside =
                    (open != std::string::npos && close != std::string::npos)
                        ? line.substr(open + 1, close - open - 1)
                        : std::string();
                std::vector<std::string> fields;
                std::size_t begin = 0;
                for (;;)
                {
                    const std::size_t comma = inside.find(',', begin);
                    fields.push_back(trimCopy(inside.substr(begin, comma == std::string::npos
                                                                   ? std::string::npos
                                                                   : comma - begin)));
                    if (comma == std::string::npos)
                    {
                        break;
                    }
                    begin = comma + 1;
                }
                if (!fields.empty() && !fields[0].empty())
                {
                    const std::string methodName = fields[0];
                    for (std::size_t index = 1; index < fields.size(); ++index)
                    {
                        if (!fields[index].empty())
                        {
                            markers.back().instantiateTypes[methodName].push_back(fields[index]);
                        }
                    }
                }
                continue;
            }
            if (!pendingBind)
            {
                // `template` 后面不一定跟着模板类：模板成员函数的声明行会带 `(` / `;`（例如
                // `template<typename T>` 的下一行 `T GetAs() const;`）。看到这种行就把模板标记
                // 清掉，否则它会把后面无关的 CA_BIND 类误判成模板类（实测：BindNativeObject
                // 被报"① 模板/泛型"）。
                if (line.find(';') != std::string::npos || line.find('(') != std::string::npos)
                {
                    pendingTemplate = false;
                }
                continue;
            }
            const bool isTemplateLine = pendingTemplate;
            const std::size_t classAt = line.find("class ");
            const std::size_t structAt = line.find("struct ");
            if (classAt == std::string::npos && structAt == std::string::npos)
            {
                // `template <typename T>` 与 `class X` 常常分两行：模板标记先记下，继续等 class 行
                continue;
            }
            const bool isClass = classAt != std::string::npos;
            const std::size_t at = isClass ? classAt : structAt;
            std::size_t nameBegin = at + (isClass ? 6u : 7u);
            while (nameBegin < line.size() && line[nameBegin] == ' ')
            {
                ++nameBegin;
            }
            std::size_t nameEnd = nameBegin;
            while (nameEnd < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[nameEnd])) || line[nameEnd] == '_' ||
                    line[nameEnd] == ':'))
            {
                ++nameEnd;
            }
            std::string name = line.substr(nameBegin, nameEnd - nameBegin);
            const std::size_t colon = name.rfind("::");
            if (colon != std::string::npos)
            {
                name = name.substr(colon + 2);
            }
            if (!name.empty())
            {
                BoundMarker marker;
                marker.interfaceName = name;
                marker.isTemplate = isTemplateLine;
                marker.reverse = pendingReverse;
                marker.hasNumber = pendingHasNumber;
                marker.number = pendingNumber;
                marker.numberText = pendingNumberText;
                markers.push_back(std::move(marker));
            }
            pendingBind = false;
            pendingReverse = false;
            pendingTemplate = false;
            pendingHasNumber = false;
            pendingNumber = 0;
            pendingNumberText.clear();
        }
        return markers;
    }

    // ---- 纯标量记录镜像（本 change 任务 3.1）----
    // 规则：成员可以是标量或另一个纯标量记录；递归收集嵌套类型，清单保留"类型 + 字段路径 + 真实偏移"。
    // 任何一层出现字符串 / 容器 / 指针 / 智能指针 / 自定义所有权成员 ⇒ 非零退出并点名完整路径。
    struct MirrorField
    {
        std::string name;
        std::string cType;          // C 镜像里的类型：标量原样 / 嵌套 `<Name>C` / 枚举底层类型
        std::string cppQualified;   // C++ 侧字段类型（限定后），转换与 static_assert 用
        std::string nestedCpp;      // 嵌套记录的 cppName（空 = 标量/枚举）
        uint64_t offsetBytes = 0;
        // 枚举字段（任务 2.2）：普通枚举按真实底层类型镜像，EnumFlags 按 MaskType 镜像。
        // 公开 C# 记录里两种都还原成公开枚举类型；isFlags 决定转换要用 `uenum::EnumFlags<T>`。
        bool isEnum = false;
        bool isFlags = false;
        std::string enumShort;
        std::string enumCpp;
    };

    struct MirrorType
    {
        std::string cppName;   // C++ 侧限定名（cawindow::MonitorInfo / cawindow::FloatRect）
        std::string cName;     // C 侧镜像名（MonitorInfoC / FloatRectC）
        std::string csName;    // 公开 C# 记录名（MonitorInfo / FloatRect）
        uint64_t sizeBytes = 0;
        std::vector<MirrorField> fields;
    };

    struct MirrorBuildCtx
    {
        std::vector<MirrorType>* sink = nullptr;
        std::vector<std::string>* errors = nullptr;
        std::set<std::string> built;   // cppName 去重（内层先于外层入 sink）
        std::string interfaceName;     // 只为报错时点名接口
    };

    // 枚举表在下面收集；镜像字段解析要先用到，所以先声明。
    EnumInfo const* findEnumByShort(std::string const& shortName);
    EnumInfo const* findEnumBySpelling(std::string const& spelling);

    // 游标的完整名字（含命名空间）：镜像断言要对着真实类型做，短名在命名空间里找不到（C2065）。
    std::string qualifiedCursorName(CXCursor cursor)
    {
        std::string prefix;
        for (CXCursor parent = clang_getCursorSemanticParent(cursor);
             !clang_Cursor_isNull(parent) && clang_getCursorKind(parent) == CXCursor_Namespace;
             parent = clang_getCursorSemanticParent(parent))
        {
            prefix = toStd(clang_getCursorSpelling(parent)) + "::" + prefix;
        }
        return prefix + toStd(clang_getCursorSpelling(cursor));
    }

    std::string namespacePrefixOf(std::string const& qualifiedName)
    {
        const std::size_t split = qualifiedName.rfind("::");
        return split == std::string::npos ? std::string() : qualifiedName.substr(0, split + 2);
    }

    // ---- C# 命名空间镜像与线形命名（csharp-bindings-namespace-alignment D1/D2/D6）----
    // 规则：完整限定名按 `::` 分层；每层内按 `_` 分词，每个词首字母大写、其余小写，各词直接相连。
    //   graphics_backend::GPUGraph → GraphicsBackend.GPUGraph
    //   a_b::Widget                → AB.Widget
    //   a::b::Widget               → A.B.Widget
    std::string capitalizeWord(std::string const& word)
    {
        if (word.empty())
        {
            return word;
        }
        std::string out = word;
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
        for (std::size_t i = 1; i < out.size(); ++i)
        {
            out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
        }
        return out;
    }

    std::string cSharpNamespaceSegment(std::string const& segment)
    {
        std::string out;
        std::string word;
        for (char ch : segment)
        {
            if (ch == '_')
            {
                out += capitalizeWord(word);
                word.clear();
            }
            else
            {
                word += ch;
            }
        }
        out += capitalizeWord(word);
        return out;
    }

    // 返回完整 C# 命名空间路径（含根 `Scripting.Generated.Bindings`）。
    std::string cSharpNamespaceOf(std::string const& qualifiedName)
    {
        const std::string prefix = namespacePrefixOf(qualifiedName);
        std::string path = "Scripting.Generated.Bindings";
        std::size_t begin = 0;
        while (begin < prefix.size())
        {
            const std::size_t split = prefix.find("::", begin);
            const std::size_t end = split == std::string::npos ? prefix.size() : split;
            const std::string segment = prefix.substr(begin, end - begin);
            if (!segment.empty())
            {
                path += "." + cSharpNamespaceSegment(segment);
            }
            if (split == std::string::npos)
            {
                break;
            }
            begin = split + 2;
        }
        return path;
    }

    // 返回完整 C# 类型名（命名空间路径 + 类名，类名保持原样）。
    std::string cSharpFullNameOf(std::string const& qualifiedName)
    {
        const std::string prefix = namespacePrefixOf(qualifiedName);
        return cSharpNamespaceOf(qualifiedName) + "." + qualifiedName.substr(prefix.size());
    }

    // 返回限定名线形：命名空间段保持原样、以 `__` 分隔，末尾接类名（`graphics_backend__GPUGraph`）。
    std::string qualifiedLinearName(std::string const& qualifiedName)
    {
        const std::string prefix = namespacePrefixOf(qualifiedName);
        std::string out = prefix;
        for (std::size_t i = 0; i + 1 < out.size();)
        {
            if (out[i] == ':' && out[i + 1] == ':')
            {
                out.replace(i, 2, "__");
                i += 2;
            }
            else
            {
                ++i;
            }
        }
        out += qualifiedName.substr(prefix.size());
        return out;
    }

    // 宿主注入钩子名（design D2）：`CAInject__<限定名线形>__<方法>__<参数>`。
    std::string injectHookName(std::string const& ownerQualified, std::string const& method,
                               std::string const& parameter)
    {
        return "CAInject__" + qualifiedLinearName(ownerQualified) + "__" + method + "__" + parameter;
    }

    // 在已知接口清单里按短名找完整限定名（本批 1.1：同短名可有多条）。
    // 优先取与 context 同命名空间的那条；找不到同命名空间的、且全表只有一条时返回它；
    // 多条且无法靠命名空间区分时返回空串（调用方按"找不到/歧义"处理，不许瞎猜）。
    std::string knownInterfaceQualified(std::string const& shortName,
                                        std::string const& contextQualified = std::string())
    {
        const std::string contextNs = namespacePrefixOf(contextQualified);
        std::string first;
        std::string sameNamespace;
        int matches = 0;
        for (auto const& entry : g_knownInterfaces)
        {
            if (entry.first != shortName)
            {
                continue;
            }
            ++matches;
            if (first.empty())
            {
                first = entry.second;
            }
            if (!contextNs.empty() && namespacePrefixOf(entry.second) == contextNs)
            {
                if (!sameNamespace.empty() && sameNamespace != entry.second)
                {
                    return std::string();
                }
                sameNamespace = entry.second;
            }
        }
        if (matches == 0)
        {
            return std::string();
        }
        return sameNamespace.empty() ? first : sameNamespace;
    }

    std::string shortTypeName(std::string const& spelling)
    {
        std::string text = trimCopy(spelling);
        const std::size_t less = text.find('<');
        if (less != std::string::npos)
        {
            text = text.substr(0, less);
        }
        const std::size_t split = text.rfind("::");
        if (split != std::string::npos)
        {
            text = text.substr(split + 2);
        }
        return text;
    }

    std::string qualifyTypeSpelling(std::string const& spelling, std::string const& prefix)
    {
        const std::string text = trimCopy(spelling);
        if (text.find("::") != std::string::npos || text.find('<') != std::string::npos || prefix.empty())
        {
            return text;
        }
        return prefix + text;
    }

    bool isNonRecordMemberType(std::string const& bare, std::string const& spelling)
    {
        static char const* const rejected[] = {
            "castl::string", "std::string", "castl::string_view", "std::string_view", "castl::vector",
            "std::vector", "castl::map", "std::map", "castl::unordered_map", "std::unordered_map",
            "castl::set", "std::set", "castl::unordered_set", "std::unordered_set", "castl::shared_ptr",
            "std::shared_ptr", "castl::unique_ptr", "std::unique_ptr", "castl::weak_ptr", "std::weak_ptr",
        };
        for (char const* prefix : rejected)
        {
            if (hasPrefix(bare, prefix))
            {
                return true;
            }
        }
        return spelling.find('*') != std::string::npos || spelling.find('&') != std::string::npos;
    }

    // 记录类型的数据布局条件（csharp-render-binding-foundations 任务 2.2 / specs「枚举、标志位与
    // 公开数据记录的投影必须保持真实布局」）：公开字段、标准布局近似、可默认构造、无虚函数；
    // 静态工厂、默认/自定义比较、非虚便利方法**不**阻止镜像。
    bool recordMethodsAllowed(CXCursor decl, std::string const& display, std::string* whyNot)
    {
        if (clang_Cursor_isNull(decl) || clang_isCursorDefinition(decl) == 0)
        {
            return true;
        }
        if (clang_getCursorKind(decl) == CXCursor_UnionDecl)
        {
            *whyNot = "类型 " + display + " 是联合体（不能按公开数据记录镜像）";
            return false;
        }
        struct MethodWalk
        {
            std::string display;
            std::string whyNot;
            bool sawConstructor = false;
            bool hasDefaultConstructor = false;
        } state{ display, {}, false, false };
        clang_visitChildren(
            decl,
            [](CXCursor member, CXCursor, CXClientData data) -> CXChildVisitResult {
                auto* walk = static_cast<MethodWalk*>(data);
                const CXCursorKind kind = clang_getCursorKind(member);
                if (kind == CXCursor_CXXBaseSpecifier)
                {
                    walk->whyNot = "类型 " + walk->display + " 带基类（公开数据记录必须是标准布局）";
                    return CXChildVisit_Break;
                }
                if (kind == CXCursor_FieldDecl)
                {
                    if (clang_getCXXAccessSpecifier(member) != CX_CXXPublic)
                    {
                        walk->whyNot = "类型 " + walk->display + " 的成员 " +
                                       toStd(clang_getCursorSpelling(member)) + " 不是公开字段（非公开字段不能镜像）";
                        return CXChildVisit_Break;
                    }
                    return CXChildVisit_Continue;
                }
                if (kind == CXCursor_CXXMethod || kind == CXCursor_Destructor)
                {
                    if (clang_CXXMethod_isVirtual(member) != 0)
                    {
                        const std::string name = toStd(clang_getCursorSpelling(member));
                        walk->whyNot = "类型 " + walk->display + " 带虚函数 " + name +
                                       "（公开数据记录不允许虚函数）";
                        return CXChildVisit_Break;
                    }
                    return CXChildVisit_Continue;
                }
                if (kind == CXCursor_Constructor)
                {
                    walk->sawConstructor = true;
                    const int arguments = clang_Cursor_getNumArguments(member);
                    if (arguments == 0)
                    {
                        walk->hasDefaultConstructor = true;
                    }
                    return CXChildVisit_Continue;
                }
                return CXChildVisit_Continue;
            },
            &state);
        if (!state.whyNot.empty())
        {
            *whyNot = state.whyNot;
            return false;
        }
        if (state.sawConstructor && !state.hasDefaultConstructor)
        {
            *whyNot = "类型 " + display + " 没有可用的默认构造函数（公开数据记录必须可默认构造）";
            return false;
        }
        return true;
    }

    bool buildMirror(MirrorBuildCtx& ctx, CXType recordType, std::string const& cppQualified,
                     std::string const& path);

    struct MirrorFieldCtx
    {
        MirrorBuildCtx* builder = nullptr;
        MirrorType* mirror = nullptr;
        std::string prefix;
        std::string path;
        bool ok = true;
    };

    CXVisitorResult collectMirrorField(CXCursor fieldCursor, CXClientData data)
    {
        auto* state = static_cast<MirrorFieldCtx*>(data);
        if (clang_getCursorKind(fieldCursor) != CXCursor_FieldDecl)
        {
            return CXVisit_Continue;
        }
        MirrorField field;
        field.name = toStd(clang_getCursorSpelling(fieldCursor));
        const CXType fieldType = clang_getCursorType(fieldCursor);
        const std::string spelling = trimCopy(toStd(clang_getTypeSpelling(fieldType)));
        const std::string canonicalSpelling =
            trimCopy(toStd(clang_getTypeSpelling(clang_getCanonicalType(fieldType))));
        const std::string bare = bareType(spelling);
        const long long bitOffset = clang_Cursor_getOffsetOfField(fieldCursor);
        if (bitOffset < 0)
        {
            state->builder->errors->push_back(state->path + "." + field.name + "：取不到字段偏移");
            state->ok = false;
            return CXVisit_Break;
        }
        field.offsetBytes = static_cast<uint64_t>(bitOffset) / 8u;
        field.cppQualified = qualifyTypeSpelling(spelling, state->prefix);
        if (isScalarType(bare))
        {
            field.cType = bare;
            state->mirror->fields.push_back(std::move(field));
            return CXVisit_Continue;
        }
        const std::string memberPath = state->path + "." + field.name;
        // 包装值不得嵌入记录（spec「独立包装值与字节入参必须有确定的值语义」）。
        {
            std::string hashInner;
            std::string flagsInner;
            if (isNameHashSpelling(spelling) || isPathHashSpelling(spelling))
            {
                state->builder->errors->push_back(memberPath + " 的类型是 " + spelling +
                                                  "（包装值不得嵌入记录）");
                state->ok = false;
                return CXVisit_Break;
            }
            if (parseHashObjWrapper(spelling, &hashInner))
            {
                state->builder->errors->push_back(memberPath + " 的类型是 " + spelling +
                                                  "（HashObj 包装值不得嵌入记录）");
                state->ok = false;
                return CXVisit_Break;
            }
        }
        if (isNonRecordMemberType(bare, spelling))
        {
            state->builder->errors->push_back(memberPath + " 的类型是 " + spelling +
                                              "（字符串 / 容器 / 指针 / 智能指针不是纯标量记录）");
            state->ok = false;
            return CXVisit_Break;
        }
        // 普通枚举：按真实底层类型镜像；EnumFlags<T>：按 MaskType 镜像。
        {
            std::string flagsInner;
            if (parseFlagsWrapper(spelling, &flagsInner) || parseFlagsWrapper(canonicalSpelling, &flagsInner))
            {
                EnumInfo const* info = findEnumByShort(flagsInner);
                if (info == nullptr)
                {
                    state->builder->errors->push_back(memberPath + "：找不到枚举 " + flagsInner + " 的定义");
                    state->ok = false;
                    return CXVisit_Break;
                }
                field.isFlags = true;
                field.isEnum = true;
                field.enumShort = info->shortName;
                field.enumCpp = info->cppName;
                field.cType = info->underlying;
                if (!isEnumUnderlyingSpelling(info->underlying))
                {
                    state->builder->errors->push_back(memberPath + "：枚举 " + info->cppName +
                                                      " 的底层类型 " + info->underlying + " 不是定宽整数");
                    state->ok = false;
                    return CXVisit_Break;
                }
                state->mirror->fields.push_back(std::move(field));
                return CXVisit_Continue;
            }
        }
        const CXType canonical = clang_getCanonicalType(fieldType);
        if (canonical.kind == CXType_Enum)
        {
            EnumInfo const* info = findEnumBySpelling(spelling);
            if (info == nullptr)
            {
                state->builder->errors->push_back(memberPath + "：找不到枚举 " + spelling + " 的定义");
                state->ok = false;
                return CXVisit_Break;
            }
            field.isEnum = true;
            field.enumShort = info->shortName;
            field.enumCpp = info->cppName;
            field.cType = info->underlying;
            if (!isEnumUnderlyingSpelling(info->underlying))
            {
                state->builder->errors->push_back(memberPath + "：枚举 " + info->cppName +
                                                  " 的底层类型 " + info->underlying + " 不是定宽整数");
                state->ok = false;
                return CXVisit_Break;
            }
            state->mirror->fields.push_back(std::move(field));
            return CXVisit_Continue;
        }
        if (canonical.kind == CXType_Record &&
            clang_getCursorKind(clang_getTypeDeclaration(canonical)) == CXCursor_UnionDecl)
        {
            state->builder->errors->push_back(memberPath + " 的类型是联合体（不能镜像）");
            state->ok = false;
            return CXVisit_Break;
        }
        if (canonical.kind != CXType_Record)
        {
            state->builder->errors->push_back(memberPath + " 的类型是 " + spelling + "（不是标量、枚举或记录）");
            state->ok = false;
            return CXVisit_Break;
        }
        if (!buildMirror(*state->builder, canonical, field.cppQualified, memberPath))
        {
            state->ok = false;
            return CXVisit_Break;
        }
        field.cType = shortTypeName(spelling) + "C";
        field.nestedCpp = field.cppQualified;
        state->mirror->fields.push_back(std::move(field));
        return CXVisit_Continue;
    }

    bool buildMirror(MirrorBuildCtx& ctx, CXType recordType, std::string const& cppQualified,
                     std::string const& path)
    {
        if (ctx.built.count(cppQualified) > 0)
        {
            return true;
        }
        const long long size = clang_Type_getSizeOf(recordType);
        if (size <= 0)
        {
            ctx.errors->push_back(path + "：取不到大小（" + cppQualified + "）");
            return false;
        }
        MirrorType mirror;
        mirror.cppName = cppQualified;
        mirror.csName = shortTypeName(cppQualified);
        mirror.cName = mirror.csName + "C";
        mirror.sizeBytes = static_cast<uint64_t>(size);
        std::string whyNot;
        if (!recordMethodsAllowed(clang_getTypeDeclaration(recordType), cppQualified, &whyNot))
        {
            ctx.errors->push_back(path + "：" + whyNot);
            return false;
        }
        MirrorFieldCtx state;
        state.builder = &ctx;
        state.mirror = &mirror;
        state.prefix = namespacePrefixOf(cppQualified);
        state.path = path;
        clang_Type_visitFields(recordType, collectMirrorField, &state);
        if (!state.ok)
        {
            return false;
        }
        // 内层先入 sink：生成 C/C# 结构体时"被嵌套的类型"必须先定义。
        ctx.sink->push_back(std::move(mirror));
        ctx.built.insert(cppQualified);
        return true;
    }

    // 把 TU 里的结构体定义按短名收起来（布局清单的起点只有"投影结果里出现的类型"）。
    struct StructCursorCollector
    {
        std::map<std::string, CXCursor>* sink = nullptr;
    };

    CXChildVisitResult collectStructCursorsForMirror(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* collector = static_cast<StructCursorCollector*>(data);
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if ((kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl) &&
            clang_isCursorDefinition(cursor) != 0)
        {
            const std::string name = toStd(clang_getCursorSpelling(cursor));
            if (!name.empty() && collector->sink->count(name) == 0)
            {
                (*collector->sink)[name] = cursor;
            }
        }
        return CXChildVisit_Recurse;
    }

    // ---- 枚举收集（csharp-render-binding-foundations 任务 2.2）----
    // 只收命名空间作用域里的枚举定义：记录字段、方法参数/返回需要的都是这一类。
    struct EnumWalkState
    {
        std::string prefix;
        std::vector<EnumInfo>* sink = nullptr;
    };

    CXChildVisitResult collectEnumConstants(CXCursor cursor, CXCursor, CXClientData data)
    {
        if (clang_getCursorKind(cursor) != CXCursor_EnumConstantDecl)
        {
            return CXChildVisit_Continue;
        }
        auto* info = static_cast<EnumInfo*>(data);
        EnumConstantInfo constant;
        constant.name = toStd(clang_getCursorSpelling(cursor));
        constant.value = clang_getEnumConstantDeclValue(cursor);
        info->constants.push_back(std::move(constant));
        return CXChildVisit_Continue;
    }

    CXChildVisitResult collectEnumsVisitor(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* state = static_cast<EnumWalkState*>(data);
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_Namespace)
        {
            EnumWalkState inner;
            inner.prefix = state->prefix + toStd(clang_getCursorSpelling(cursor)) + "::";
            inner.sink = state->sink;
            clang_visitChildren(cursor, collectEnumsVisitor, &inner);
            return CXChildVisit_Continue;
        }
        if (kind == CXCursor_EnumDecl && clang_isCursorDefinition(cursor) != 0)
        {
            EnumInfo info;
            info.shortName = toStd(clang_getCursorSpelling(cursor));
            if (info.shortName.empty())
            {
                return CXChildVisit_Continue;
            }
            info.cppName = state->prefix + info.shortName;
            info.underlying = trimCopy(toStd(clang_getTypeSpelling(clang_getEnumDeclIntegerType(cursor))));
            clang_visitChildren(cursor, collectEnumConstants, &info);
            state->sink->push_back(std::move(info));
        }
        return CXChildVisit_Continue;
    }

    EnumInfo const* findEnumByShort(std::string const& shortName)
    {
        auto found = g_enumsByShort.find(shortName);
        return found == g_enumsByShort.end() ? nullptr : &found->second;
    }

    EnumInfo const* findEnumBySpelling(std::string const& spelling)
    {
        const std::string bare = bareType(spelling);
        if (isEnumUnderlyingSpelling(bare) || bare == "bool")
        {
            return nullptr;
        }
        const std::size_t split = bare.rfind("::");
        const std::string shortName = split == std::string::npos ? bare : bare.substr(split + 2);
        return findEnumByShort(shortName);
    }

    // ---- 共享公开值类型产物（任务 2.3 / design 决策三）----
    // 每个限定 C++ 值类型只发一份 `<类型短名>.Bindings.cs`；内容只由该类型自己的声明决定，
    // 因此同一类型被多个接口需要时逐字节相同。文件头写限定名，供 build.py 做跨输入的哈希对账。
    std::string mirrorFieldCSharpSurfaceType(MirrorField const& field);
    std::string emitPublicRecordFile(MirrorType const& mirror)
    {
        std::string cs;
        cs += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
        cs += "// 共享公开值类型：同一限定 C++ 类型的唯一一份公开 C# 产物。\n";
        cs += "// 限定名：" + mirror.cppName + "\n";
        cs += "using System;\nusing System.Runtime.InteropServices;\n\n";
        cs += "namespace " + cSharpNamespaceOf(mirror.cppName) + "\n{\n";
        cs += "    /// <summary>" + mirror.cppName + " 的公开记录：字段名与顺序与 C++ 声明一致</summary>\n";
        cs += "    [StructLayout(LayoutKind.Sequential)]\n";
        cs += "    public struct " + mirror.csName + "\n    {\n";
        for (MirrorField const& field : mirror.fields)
        {
            if (field.cType == "bool")
            {
                cs += "        [MarshalAs(UnmanagedType.I1)]\n";
            }
            cs += "        public " + mirrorFieldCSharpSurfaceType(field) + " " + field.name + ";\n";
        }
        cs += "    }\n}\n";
        return cs;
    }

    std::string emitPublicEnumFile(EnumInfo const& info)
    {
        std::string cs;
        cs += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
        cs += "// 共享公开值类型：同一限定 C++ 类型的唯一一份公开 C# 产物。\n";
        cs += "// 限定名：" + info.cppName + "\n";
        cs += "using System;\nusing System.Runtime.InteropServices;\n\n";
        cs += "namespace " + cSharpNamespaceOf(info.cppName) + "\n{\n";
        if (info.usedAsFlags)
        {
            cs += "    [Flags]\n";
        }
        const std::string underlying = cSharpIntegerType(info.underlying);
        cs += "    public enum " + info.shortName + " : " + underlying + "\n    {\n";
        for (EnumConstantInfo const& constant : info.constants)
        {
            cs += "        " + constant.name + " = " + std::to_string(constant.value) + ",\n";
        }
        cs += "    }\n}\n";
        return cs;
    }

    // C++ 侧：镜像结构体 + 对着真实类型的 static_assert + 双向转换 helper + 运行期清单入口。
    // C# 侧：同类常量、内嵌的 C 形状与公开记录、以及托管启动时逐项比对的清单数组。
    bool mirrorNeedsPublicRecord(std::vector<MirrorType> const& mirrors, std::string const& cppName)
    {
        for (MirrorType const& mirror : mirrors)
        {
            for (MirrorField const& field : mirror.fields)
            {
                if (field.nestedCpp.empty())
                {
                    continue;
                }
                if (field.nestedCpp == cppName || mirror.cppName == cppName)
                {
                    return true;
                }
            }
        }
        return false;
    }

    std::string mirrorFieldCSharpCType(std::string const& cType)
    {
        if (cType == "bool")
        {
            return "byte";   // C 侧 `bool` 一个字节；托管结构体用 byte 保证布局一致，转换时再还原 bool
        }
        // 嵌套镜像的 C 类型名（`<Name>C`）原样保留：它就在同一个 `<接口>Bindings` 类里。
        if (cType.size() > 1 && cType.back() == 'C' && !isScalarType(cType))
        {
            return cType;
        }
        return toCSharpType(cType);
    }

    std::string mirrorFieldCSharpSurfaceType(MirrorField const& field)
    {
        if (field.isEnum)
        {
            return field.enumShort;
        }
        if (!field.nestedCpp.empty())
        {
            return shortTypeName(field.nestedCpp);
        }
        const std::string type = toCSharpType(field.cType);
        return type.empty() ? field.cType : type;
    }

    void emitMirrorCSharpTypes(std::string* cs, std::vector<MirrorType> const& mirrors)
    {
        for (MirrorType const& mirror : mirrors)
        {
            if (!mirrorNeedsPublicRecord(mirrors, mirror.cppName))
            {
                continue;
            }
            *cs += "    /// <summary>" + mirror.cppName + " 的公开记录：字段名与顺序与 C++ 声明一致</summary>\n";
            *cs += "    [StructLayout(LayoutKind.Sequential)]\n";
            *cs += "    public struct " + mirror.csName + "\n    {\n";
            for (MirrorField const& field : mirror.fields)
            {
                if (field.cType == "bool")
                {
                    *cs += "        [MarshalAs(UnmanagedType.I1)]\n";
                }
                *cs += "        public " + mirrorFieldCSharpSurfaceType(field) + " " + field.name + ";\n";
            }
            *cs += "    }\n\n";
        }
    }

    void emitMirrorCSharpStructs(std::string* cs, std::vector<MirrorType> const& mirrors)
    {
        for (MirrorType const& mirror : mirrors)
        {
            *cs += "        /// <summary>" + mirror.cppName + " 的 C 形状（native 侧逐字节同源）</summary>\n";
            *cs += "        [StructLayout(LayoutKind.Sequential)]\n";
            *cs += "        internal struct " + mirror.cName + "\n        {\n";
            for (MirrorField const& field : mirror.fields)
            {
                *cs += "            public " + mirrorFieldCSharpCType(field.cType) + " " + field.name + ";\n";
            }
            *cs += "        }\n";
        }
    }

    // C# 侧的 FromC / ToC：放在 `<接口>Calls` 里，负责公开记录与 C 形状之间的逐字段搬运。
    void emitMirrorCSharpConversions(std::string* cs, std::string const& interfaceName,
                                     std::vector<MirrorType> const& mirrors)
    {
        for (MirrorType const& mirror : mirrors)
        {
            // 公开记录现在按类型共享产出（任务 2.3），每个接口的 Calls 层都要有自己那份
            // FromC / ToC —— 它们引用的是 `<接口>Bindings` 里的 C 镜像，不能跨接口复用。
            const std::string cType = interfaceName + "Bindings." + mirror.cName;
            *cs += "        internal static " + mirror.csName + " FromC" + mirror.csName + "(" + cType +
                   " value)\n        {\n";
            *cs += "            " + mirror.csName + " result = new " + mirror.csName + "();\n";
            for (MirrorField const& field : mirror.fields)
            {
                if (!field.nestedCpp.empty())
                {
                    *cs += "            result." + field.name + " = FromC" + shortTypeName(field.nestedCpp) +
                           "(value." + field.name + ");\n";
                }
                else if (field.isEnum)
                {
                    *cs += "            result." + field.name + " = (" + field.enumShort + ")value." + field.name +
                           ";\n";
                }
                else if (field.cType == "bool")
                {
                    *cs += "            result." + field.name + " = value." + field.name + " != 0;\n";
                }
                else
                {
                    *cs += "            result." + field.name + " = value." + field.name + ";\n";
                }
            }
            *cs += "            return result;\n        }\n";
            *cs += "        internal static " + cType + " ToC" + mirror.csName + "(" + mirror.csName +
                   " value)\n        {\n";
            *cs += "            " + cType + " result = new " + cType + "();\n";
            for (MirrorField const& field : mirror.fields)
            {
                if (!field.nestedCpp.empty())
                {
                    *cs += "            result." + field.name + " = ToC" + shortTypeName(field.nestedCpp) +
                           "(value." + field.name + ");\n";
                }
                else if (field.isEnum)
                {
                    *cs += "            result." + field.name + " = (" + mirrorFieldCSharpCType(field.cType) +
                           ")value." + field.name + ";\n";
                }
                else if (field.cType == "bool")
                {
                    *cs += "            result." + field.name + " = (byte)(value." + field.name + " ? 1 : 0);\n";
                }
                else
                {
                    *cs += "            result." + field.name + " = value." + field.name + ";\n";
                }
            }
            *cs += "            return result;\n        }\n";
        }
    }

    void emitLayoutManifest(std::string* cpp, std::string* cs, std::string const& interfaceName,
                            std::vector<MirrorType> const& mirrors)
    {
        for (MirrorType const& mirror : mirrors)
        {
            // 同一份 C 镜像结构体会出现在多个产物里（`GPUBufferDescriptorC` 同时被桥的
            // 渲染图的表与原生 GPUGraph 需要），宿主把所有产物 include 进同一个翻译单元
            // 时就是重定义（实测 `error C2011: 'GPUBufferDescriptorC': 'struct' type redefinition`）。
            // 用宏守卫按类型名去重；断言也一起包进去（重复的 static_assert 无害但没必要）。
            const std::string guard = "CODEGEN_MIRROR_" + mirror.cName;
            *cpp += "#ifndef " + guard + "\n#define " + guard + "\n";
            *cpp += "typedef struct " + mirror.cName + " {\n";
            for (MirrorField const& field : mirror.fields)
            {
                *cpp += "    " + field.cType + " " + field.name + ";\n";
            }
            *cpp += "} " + mirror.cName + ";\n";
            // 标准布局 / 默认可构造：生成器只做了近似判定，真实语义由编译器在这里钉死。
            *cpp += "static_assert(std::is_standard_layout_v<" + mirror.cppName +
                    ">, \"公开数据记录必须是标准布局\");\n";
            *cpp += "static_assert(std::is_default_constructible_v<" + mirror.cppName +
                    ">, \"公开数据记录必须可默认构造\");\n";
            // 关键：断言要**对着 C++ 侧的真实类型**做。只比"镜像 vs 镜像自己算出来的常量"是假判据 ——
            // 生成器算错布局时两边一起错、照样绿；只有把真实类型拉进来，头里改了字段而不重新生成才会红。
            *cpp += "static_assert(sizeof(" + mirror.cppName + ") == " + std::to_string(mirror.sizeBytes) +
                    ", \"布局清单与 C++ 侧不一致：\" \"" + mirror.cppName + " 的真实大小变了，重新生成绑定\");\n";
            for (MirrorField const& field : mirror.fields)
            {
                *cpp += "static_assert(offsetof(" + mirror.cppName + ", " + field.name + ") == " +
                        std::to_string(field.offsetBytes) + ", \"布局清单与 C++ 侧不一致：\" \"" +
                        mirror.cppName + "." + field.name + " 的真实偏移变了，重新生成绑定\");\n";
            }
            *cpp += "static_assert(sizeof(" + mirror.cName + ") == " + std::to_string(mirror.sizeBytes) +
                    ", \"镜像结构体与清单不一致：\" \"" + mirror.cName + " 的大小\");\n";
            for (MirrorField const& field : mirror.fields)
            {
                *cpp += "static_assert(offsetof(" + mirror.cName + ", " + field.name + ") == " +
                        std::to_string(field.offsetBytes) + ", \"布局清单与 C++ 侧不一致：\" \"" +
                        mirror.cName + "." + field.name + " 的偏移\");\n";
            }
            // 双向转换：嵌套记录递归展开，不假设紧凑排列。
            // 转换函数是**按接口**命名的（`<接口>_fromC_<记录>`），不进守卫 —— 每个产物都要有自己的一份。
            *cpp += "#endif\n";
            *cpp += "static " + mirror.cppName + " " + interfaceName + "_fromC_" + mirror.csName + "(" +
                    mirror.cName + " const& value)\n{\n";
            *cpp += "    " + mirror.cppName + " result{};\n";
            for (MirrorField const& field : mirror.fields)
            {
                if (!field.nestedCpp.empty())
                {
                    *cpp += "    result." + field.name + " = " + interfaceName + "_fromC_" +
                            shortTypeName(field.nestedCpp) + "(value." + field.name + ");\n";
                }
                else if (field.isEnum && field.isFlags)
                {
                    *cpp += "    result." + field.name + " = ::uenum::EnumFlags<" + field.enumCpp + ">(value." +
                            field.name + ");\n";
                }
                else if (field.isEnum)
                {
                    *cpp += "    result." + field.name + " = static_cast<" + field.enumCpp + ">(value." + field.name +
                            ");\n";
                }
                else if (field.cType == "bool")
                {
                    *cpp += "    result." + field.name + " = (value." + field.name + " != 0);\n";
                }
                else
                {
                    *cpp += "    result." + field.name + " = value." + field.name + ";\n";
                }
            }
            *cpp += "    return result;\n}\n";
            *cpp += "static " + mirror.cName + " " + interfaceName + "_toC_" + mirror.csName + "(" +
                    mirror.cppName + " const& value)\n{\n";
            *cpp += "    " + mirror.cName + " result{};\n";
            for (MirrorField const& field : mirror.fields)
            {
                if (!field.nestedCpp.empty())
                {
                    *cpp += "    result." + field.name + " = " + interfaceName + "_toC_" +
                            shortTypeName(field.nestedCpp) + "(value." + field.name + ");\n";
                }
                else if (field.isEnum && field.isFlags)
                {
                    *cpp += "    result." + field.name + " = static_cast<" + field.cType + ">(value." + field.name +
                            ".m_mask);\n";
                }
                else if (field.isEnum)
                {
                    *cpp += "    result." + field.name + " = static_cast<" + field.cType + ">(value." + field.name +
                            ");\n";
                }
                else if (field.cType == "bool")
                {
                    *cpp += "    result." + field.name + " = static_cast<bool>(value." + field.name + ");\n";
                }
                else
                {
                    *cpp += "    result." + field.name + " = value." + field.name + ";\n";
                }
            }
            *cpp += "    return result;\n}\n\n";
            *cs += "        // 布局清单（与 C++ 侧同源常量；不一致由 C++ 的 static_assert 与托管启动自检兜住）\n";
            *cs += "        public const int " + mirror.cName + "_Size = " +
                   std::to_string(mirror.sizeBytes) + ";\n";
            for (MirrorField const& field : mirror.fields)
            {
                *cs += "        public const int " + mirror.cName + "_" + field.name + "_Offset = " +
                       std::to_string(field.offsetBytes) + ";\n";
            }
        }
        // 托管启动自检用的清单：类型名、大小、每个字段的名字 / C 类型 / 偏移。
        // 与引擎侧运行期清单逐项比对（`Host.Boot`），不一致就拒绝继续初始化。
        *cs += "        /// <summary>布局清单：托管启动时与引擎侧逐项比对，不一致拒绝绑定</summary>\n";
        *cs += "        internal static readonly BindingLayoutType[] LayoutManifest = new BindingLayoutType[]\n        {\n";
        for (MirrorType const& mirror : mirrors)
        {
            *cs += "            new BindingLayoutType { Name = \"" + mirror.cName + "\", Size = " +
                   std::to_string(mirror.sizeBytes) + ", Fields = new BindingLayoutField[]\n            {\n";
            for (MirrorField const& field : mirror.fields)
            {
                *cs += "                new BindingLayoutField { Name = \"" + field.name + "\", Type = \"" +
                       field.cType + "\", Offset = " + std::to_string(field.offsetBytes) + " },\n";
            }
            *cs += "            } },\n";
        }
        *cs += "        };\n";
        // 运行期清单入口：托管启动时逐项比对（真实 sizeof / offsetof 在这里生成，不用手算常量）。
        // 没有镜像类型的接口也必须导出同名入口（返回 0）—— 托管侧对两个接口都调同一组函数。
        if (mirrors.empty())
        {
            *cpp += "extern \"C\" uint32_t " + interfaceName + "__GetLayoutTypeCount(void)\n{\n    return 0u;\n}\n";
            *cpp += "extern \"C\" uint32_t " + interfaceName +
                    "__GetLayoutType(uint32_t, char*, uint32_t, uint64_t*, uint32_t*)\n{\n    return 0u;\n}\n";
            *cpp += "extern \"C\" uint32_t " + interfaceName +
                    "__GetLayoutField(uint32_t, uint32_t, char*, uint32_t, char*, uint32_t, uint64_t*)\n{\n"
                    "    return 0u;\n}\n\n";
            return;
        }
        *cpp += "typedef struct " + interfaceName + "LayoutFieldData { char const* name; char const* typeName; "
                "uint64_t offset; } " + interfaceName + "LayoutFieldData;\n";
        *cpp += "typedef struct " + interfaceName + "LayoutTypeData { char const* name; uint64_t sizeBytes; "
                "uint32_t fieldCount; " + interfaceName + "LayoutFieldData const* fields; } " +
                interfaceName + "LayoutTypeData;\n";
        for (std::size_t index = 0; index < mirrors.size(); ++index)
        {
            MirrorType const& mirror = mirrors[index];
            *cpp += "static const " + interfaceName + "LayoutFieldData " + interfaceName + "_layoutFields" +
                    std::to_string(index) + "[] = {\n";
            for (MirrorField const& field : mirror.fields)
            {
                *cpp += "    { \"" + field.name + "\", \"" + field.cType + "\", offsetof(" + mirror.cName +
                        ", " + field.name + ") },\n";
            }
            *cpp += "};\n";
        }
        *cpp += "static const " + interfaceName + "LayoutTypeData " + interfaceName + "_layoutTypes[] = {\n";
        for (std::size_t index = 0; index < mirrors.size(); ++index)
        {
            MirrorType const& mirror = mirrors[index];
            *cpp += "    { \"" + mirror.cName + "\", sizeof(" + mirror.cName + "), " +
                    std::to_string(mirror.fields.size()) + ", " + interfaceName + "_layoutFields" +
                    std::to_string(index) + " },\n";
        }
        *cpp += "};\n";
        *cpp += "extern \"C\" uint32_t " + interfaceName + "__GetLayoutTypeCount(void)\n{\n    return " +
                std::to_string(mirrors.size()) + "u;\n}\n";
        *cpp += "extern \"C\" uint32_t " + interfaceName +
                "__GetLayoutType(uint32_t index, char* name, uint32_t nameCap, uint64_t* sizeBytes, "
                "uint32_t* fieldCount)\n{\n";
        *cpp += "    if (index >= " + std::to_string(mirrors.size()) + "u) { return 0u; }\n";
        *cpp += "    const " + interfaceName + "LayoutTypeData& type = " + interfaceName + "_layoutTypes[index];\n";
        *cpp += "    if (name == nullptr || nameCap == 0u) { return 0u; }\n";
        *cpp += "    uint32_t written = 0u;\n";
        *cpp += "    while (written + 1u < nameCap && type.name[written] != 0) { name[written] = type.name[written]; ++written; }\n";
        *cpp += "    name[written] = 0;\n";
        *cpp += "    if (sizeBytes != nullptr) { *sizeBytes = type.sizeBytes; }\n";
        *cpp += "    if (fieldCount != nullptr) { *fieldCount = type.fieldCount; }\n";
        *cpp += "    return 1u;\n}\n";
        *cpp += "extern \"C\" uint32_t " + interfaceName +
                "__GetLayoutField(uint32_t typeIndex, uint32_t fieldIndex, char* name, uint32_t nameCap, "
                "char* typeName, uint32_t typeCap, uint64_t* offsetBytes)\n{\n";
        *cpp += "    if (typeIndex >= " + std::to_string(mirrors.size()) + "u) { return 0u; }\n";
        *cpp += "    const " + interfaceName + "LayoutTypeData& type = " + interfaceName + "_layoutTypes[typeIndex];\n";
        *cpp += "    if (fieldIndex >= type.fieldCount) { return 0u; }\n";
        *cpp += "    const " + interfaceName + "LayoutFieldData& field = type.fields[fieldIndex];\n";
        *cpp += "    if (name == nullptr || nameCap == 0u || typeName == nullptr || typeCap == 0u) { return 0u; }\n";
        *cpp += "    uint32_t written = 0u;\n";
        *cpp += "    while (written + 1u < nameCap && field.name[written] != 0) { name[written] = field.name[written]; ++written; }\n";
        *cpp += "    name[written] = 0;\n";
        *cpp += "    written = 0u;\n";
        *cpp += "    while (written + 1u < typeCap && field.typeName[written] != 0) { typeName[written] = field.typeName[written]; ++written; }\n";
        *cpp += "    typeName[written] = 0;\n";
        *cpp += "    if (offsetBytes != nullptr) { *offsetBytes = field.offset; }\n";
        *cpp += "    return 1u;\n}\n\n";
    }

    struct UnboundCollector
    {
        std::vector<std::string> const* bound = nullptr;
        std::vector<std::string>* sink = nullptr;
        std::string mainFile;   // 输入头的文件名（小写、正斜杠），只报这个文件里声明的接口
    };

    CXChildVisitResult collectUnboundInterfaces(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* collector = static_cast<UnboundCollector*>(data);
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if ((kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl) &&
            clang_isCursorDefinition(cursor) != 0)
        {
            // 有没有纯虚方法（= 是不是接口）
            bool hasPureVirtual = false;
            clang_visitChildren(
                cursor,
                [](CXCursor member, CXCursor, CXClientData flagData) {
                    if (clang_getCursorKind(member) == CXCursor_CXXMethod &&
                        clang_CXXMethod_isPureVirtual(member) != 0u)
                    {
                        *static_cast<bool*>(flagData) = true;
                        return CXChildVisit_Break;
                    }
                    return CXChildVisit_Continue;
                },
                &hasPureVirtual);
            const std::string name = toStd(clang_getCursorSpelling(cursor));
            // 只报**本文件里声明的**接口：否则标准库那些抽象类（memory_resource / _Ref_count_base）
            // 会把跳过清单刷满，真正要看的那一条反而被埋掉
            // 只报**本文件里声明的**接口：否则标准库那些抽象类（memory_resource / _Ref_count_base）
            // 会把跳过清单刷满，真正要看的那一条反而被埋掉
            CXSourceLocation location = clang_getCursorLocation(cursor);
            CXFile declFile = nullptr;
            clang_getSpellingLocation(location, &declFile, nullptr, nullptr, nullptr);
            std::string declPath = declFile != nullptr ? toStd(clang_getFileName(declFile)) : std::string();
            for (char& character : declPath)
            {
                if (character == '\\') { character = '/'; }
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            const bool inMainFile = !declPath.empty() && !collector->mainFile.empty() &&
                                    declPath.size() >= collector->mainFile.size() &&
                                    declPath.compare(declPath.size() - collector->mainFile.size(),
                                                     collector->mainFile.size(), collector->mainFile) == 0;
            const bool isBound = std::find(collector->bound->begin(), collector->bound->end(), name) !=
                                 collector->bound->end();
            if (hasPureVirtual && !isBound && inMainFile)
            {
                collector->sink->push_back(name);
            }
        }
        return CXChildVisit_Recurse;
    }
    int paramKindFor(MethodSignature const& method, std::size_t index)
    {
        return index < method.paramKinds.size() ? method.paramKinds[index] : 0;
    }

    // C ABI 类型 → C# 类型。认得的逐个映射；认不得的（镜像结构体、函数指针）先按 `void*` 走 ——
    // 真实映射与布局清单在任务 2.5 接，这里的桩是**可见的**（不是悄悄给个错类型）。
    std::string toCSharpType(std::string const& cType)
    {
        if (cType.find("(*)(") != std::string::npos)
        {
            // 回调参数（本 change 2.1）：原生函数指针 → C# 的 `delegate* unmanaged<...>`。
            // 参数表逐项映射；认不出的类型不给默认值（返回空串会拼出编不过的声明，比静默改类型安全）。
            const std::size_t argOpen = cType.find('(', cType.find("(*)") + 3);
            const std::size_t argClose = cType.rfind(')');
            std::string result = "delegate* unmanaged<";
            if (argOpen == std::string::npos || argClose == std::string::npos || argClose <= argOpen)
            {
                return std::string();
            }
            const std::vector<std::string> arguments =
                splitTopLevelArguments(cType.substr(argOpen + 1, argClose - argOpen - 1));
            for (std::size_t index = 0; index < arguments.size(); ++index)
            {
                const std::string bare = trimCopy(arguments[index]);
                std::string mapped;
                if (bare == "void*") { mapped = "void*"; }
                else if (bare == "int32_t" || bare == "int") { mapped = "int"; }
                else if (bare == "uint32_t") { mapped = "uint"; }
                else if (bare == "float") { mapped = "float"; }
                else if (bare == "double") { mapped = "double"; }
                else if (bare == "uint8_t") { mapped = "byte"; }
                else { return std::string(); }
                result += (index == 0 ? "" : ", ") + mapped;
            }
            result += ", void>";
            return result;
        }
        if (cType == "void")
        {
            return "void";
        }
        if (cType == "int32_t" || cType == "int")
        {
            return "int";
        }
        if (cType == "bool")
        {
            return "bool";
        }
        if (cType == "uint16_t" || cType == "unsigned short")
        {
            return "ushort";
        }
        if (cType == "int16_t" || cType == "short")
        {
            return "short";
        }
        if (cType == "uint8_t" || cType == "unsigned char")
        {
            return "byte";
        }
        if (cType == "int8_t" || cType == "signed char" || cType == "char")
        {
            return "sbyte";
        }
        if (cType == "int64_t" || cType == "long")
        {
            return "long";
        }
        if (cType == "size_t")
        {
            return "ulong";
        }
        if (cType == "uint32_t")
        {
            return "uint";
        }
        if (cType == "uint64_t")
        {
            return "ulong";
        }
        if (cType == "uint8_t")
        {
            return "byte";
        }
        if (cType == "float")
        {
            return "float";
        }
        if (cType == "double")
        {
            return "double";
        }
        if (cType == "void*")
        {
            return "void*";
        }
        if (cType == "char const*" || cType == "char*")
        {
            return "byte*";
        }
        if (cType == "uint8_t const*")
        {
            return "byte*";
        }
        if (!cType.empty() && cType.back() == '*')
        {
            std::string inner = cType.substr(0, cType.size() - 1);
            while (!inner.empty() && inner.back() == ' ')
            {
                inner.pop_back();
            }
            if (inner.size() > 6 && inner.compare(inner.size() - 6, 6, " const") == 0)
            {
                inner = inner.substr(0, inner.size() - 6);
            }
            // 结构体镜像按字节指针走；标量指针按对应标量走
            return toCSharpType(trimCopy(inner)) + "*";
        }
        return "void*";
    }

    // 已标记 CA_BIND 的接口名在文件上方提前定义（回调形状解析也要用）。

    // 把一条方法投影成 C ABI 的（参数们, 返回类型）。返回 false 表示该形状必须被拒绝。
    bool projectMethod(MethodSignature const& method, std::vector<Projected>* params, std::string* returnType,
std::string* rejectReason, bool* needsTwoStep, bool* isHandleReturn, bool* isCallback = nullptr,
std::string* twoStepRecordShort = nullptr, std::string* twoStepScalarElement = nullptr)
    {
        *needsTwoStep = false;
        *isHandleReturn = false;
        if (twoStepScalarElement != nullptr)
        {
            twoStepScalarElement->clear();
        }
        if (twoStepRecordShort != nullptr)
        {
            twoStepRecordShort->clear();
        }
        if (isCallback != nullptr)
        {
            *isCallback = false;
        }
        params->clear();
        *returnType = "void";

        // 回调参数（本 change 任务 2.1）：只允许"唯一形参就是回调"。先整体检查，再逐参投影 ——
        // 不带这个检查的话，回调跟其它形参混在一起会生成一条原生实现根本调不通的签名。
        {
            MethodCallbackInfo callbackInfo;
            std::string callbackWhyNot;
            if (analyzeCallbackMethod(method, &callbackInfo, &callbackWhyNot))
            {
                if (isCallback != nullptr)
                {
                    *isCallback = true;
                }
                params->push_back(
                    { callbackFunctionPointerType(callbackInfo.shape), callbackInfo.paramName + "Fn", true,
                      callbackInfo.shape });
                params->push_back({ "void*", callbackInfo.paramName + "User", false, {} });
                // 安装入口返回内部注册号；公开 C# setter 仍然返回 void（design D1）。
                *returnType = "uint64_t";
                return true;
            }
            for (auto const& param : method.params)
            {
                const std::string bare = bareType(param.first);
                if (hasPrefix(bare, "castl::function") || hasPrefix(bare, "std::function"))
                {
                    *rejectReason = "回调投影：" + callbackWhyNot;
                    return false;
                }
            }
        }

        for (auto const& param : method.params)
        {
            const std::string spelling = trimCopy(param.first);
            const std::string bare = bareType(spelling);
            const std::string name = param.second.empty() ? std::string("arg") : param.second;
            // 注入参数（任务 3.3）：不进 C ABI、不进托管表面；thunk 从固定名钩子取值。
            if (method.injectedParams.count(param.second) > 0)
            {
                continue;
            }

            // 内存窗（任务 1.1 / design D2）：指针参数出两段 ABI（`uint8_t const*` + 长度），
            // 配对的长度参数被吸收、不再单独出参。配对表由 run() 的配对检查填；这里只消费。
            // 两张表的键：`windowLengthByPointer` 的键是**指针**下标、值是长度下标；
            // `windowPointerByLength` 的键是**长度**下标、值是指针下标。
            {
                const std::size_t paramIndex = static_cast<std::size_t>(&param - method.params.data());
                if (method.windowLengthByPointer.count(paramIndex) > 0)
                {
                    params->push_back({"uint8_t const*", name});
                    params->push_back({"uint64_t", name + "Count"});
                    continue;
                }
                if (method.windowPointerByLength.count(paramIndex) > 0)
                {
                    continue;
                }
            }

            // 字节入参（任务 2.5）：必须在通用容器判定之前，否则会拼出不存在的 `uint8_tC`。
            if (isByteVectorSpelling(spelling))
            {
                params->push_back({"uint8_t const*", name});
                params->push_back({"uint64_t", name + "Count"});
                continue;
            }
            // 独立包装值入参（任务 2.4）。
            if (isNameHashSpelling(spelling) || isPathHashSpelling(spelling))
            {
                params->push_back({"char const*", name});
                params->push_back({"uint64_t", name + "Len"});
                continue;
            }
            {
                std::string hashInner;
                if (parseHashObjWrapper(spelling, &hashInner))
                {
                    params->push_back({shortTypeName(hashInner) + "C const*", name});
                    continue;
                }
            }
            {
                std::string flagsInner;
                const std::size_t paramIndex = static_cast<std::size_t>(&param - method.params.data());
                const std::string canonicalSpelling =
                    paramIndex < method.paramCanonicalSpellings.size() ? method.paramCanonicalSpellings[paramIndex]
                                                                      : spelling;
                if (parseFlagsWrapper(spelling, &flagsInner) || parseFlagsWrapper(canonicalSpelling, &flagsInner))
                {
                    EnumInfo const* info = findEnumByShort(flagsInner);
                    if (info == nullptr || !isEnumUnderlyingSpelling(info->underlying))
                    {
                        *rejectReason = "方法级 EnumFlags 的参数 " + name + " 找不到枚举 " + flagsInner +
                                        " 的真实底层类型";
                        return false;
                    }
                    if (spelling.find('&') != std::string::npos && spelling.find("const") == std::string::npos)
                    {
                        *rejectReason = "EnumFlags 出参不在本 change 的支持范围内（参数 " + name + "）";
                        return false;
                    }
                    params->push_back({info->underlying, name});
                    continue;
                }
            }
            // 资源引用值类（任务 3.1）：四段线形 = kind 枚举 + 对象句柄 + 名称 ptr/len，
            // 全部由既有四类线形组合，不新增 C ABI 线形类别。
            {
                auto valueType = g_valueTypes.find(bare);
                if (valueType != g_valueTypes.end())
                {
                    if (spelling.find('*') != std::string::npos)
                    {
                        *rejectReason = "资源引用值类 " + bare +
                                        " 只支持按值 / const& 参数（指针位置不在本批支持范围内）";
                        return false;
                    }
                    params->push_back({"uint32_t", name + "Kind"});
                    params->push_back({"uint64_t", name + "Handle"});
                    params->push_back({"uint8_t const*", name + "Name"});
                    params->push_back({"uint64_t", name + "NameLen"});
                    continue;
                }
            }
            // 序列化块记录（任务 3.2）：输入方向（const& / 值），过界是 uint8_t const* + count。
            // 非 const 引用是出参方向 —— 本批只定义输入方向，生成期拒绝并留名。
            {
                auto serialized = g_serializedTypes.find(bare);
                if (serialized != g_serializedTypes.end())
                {
                    if (spelling.find('*') != std::string::npos)
                    {
                        *rejectReason = "序列化块记录 " + bare + " 只支持 const& / 按值入参（指针位置不支持）";
                        return false;
                    }
                    if (spelling.find('&') != std::string::npos && spelling.find("const") == std::string::npos)
                    {
                        *rejectReason = "序列化块记录的出参方向（非 const 引用 T&）不在本批支持范围内 —— "
                                        "本批只定义输入方向；出参投影是独立形状，留给出参投影批";
                        return false;
                    }
                    params->push_back({"uint8_t const*", name + "Bytes"});
                    params->push_back({"uint64_t", name + "Len"});
                    continue;
                }
            }
            if (hasPrefix(bare, "castl::weak_ptr") || hasPrefix(bare, "std::weak_ptr"))
            {
                *rejectReason = "② 所有权不明裸指针：weak_ptr 入参不投影（参数 " + name + "）";
                return false;
            }
            if (hasPrefix(bare, "castl::shared_ptr") || hasPrefix(bare, "std::shared_ptr") ||
                hasPrefix(bare, "std::unique_ptr"))
            {
                params->push_back({"uint64_t", name + "Handle"});
                continue;
            }
            if (hasPrefix(bare, "castl::string_view") || hasPrefix(bare, "std::string_view") ||
                bare == "castl::string" || bare == "std::string")
            {
                params->push_back({"char const*", name});
                params->push_back({"uint64_t", name + "Len"});
                continue;
            }
            if (hasPrefix(bare, "castl::vector") || hasPrefix(bare, "std::vector"))
            {
                const std::size_t open = bare.find('<');
                const std::size_t close = bare.rfind('>');
                const std::string element = (open != std::string::npos && close != std::string::npos && close > open)
                                                ? bareType(bare.substr(open + 1, close - open - 1))
                                                : std::string();
                const bool isOut = spelling.find('&') != std::string::npos &&
                                   spelling.find("const") == std::string::npos;
                if (isScalarType(element) || element == "BindScalarThing" || element == "MonitorInfo")
                {
                    if (isOut)
                    {
                        params->push_back({element + "C*", name + "Dst"});
                        params->push_back({"uint64_t", name + "Cap"});
                    }
                    else
                    {
                        params->push_back({element + "C const*", name});
                        params->push_back({"uint64_t", name + "Count"});
                    }
                }
                else
                {
                    params->push_back({"uint8_t const*", name + "Bytes"});
                    params->push_back({"uint64_t", name + "Len"});
                }
                continue;
            }
            if (hasPrefix(bare, "castl::unordered_map") || hasPrefix(bare, "castl::map") ||
                hasPrefix(bare, "castl::set") || hasPrefix(bare, "std::unordered_map"))
            {
                const std::size_t open = bare.find('<');
                const std::size_t close = bare.rfind('>');
                const std::string inside = (open != std::string::npos && close != std::string::npos && close > open)
                                               ? bare.substr(open + 1, close - open - 1)
                                               : std::string();
                const std::size_t comma = inside.find(',');
                const std::string key = trimCopy(comma == std::string::npos ? inside : inside.substr(0, comma));
                const std::string value = trimCopy(comma == std::string::npos ? std::string()
                                                                             : inside.substr(comma + 1));
                // 标量键/值不带镜像后缀（`uint32_t const*`），只有记录类型才加 `C`
                const auto mirror = [](std::string const& t) { return isScalarType(t) ? t : t + "C"; };
                params->push_back({mirror(key) + " const*", name + "Keys"});
                if (!value.empty())
                {
                    params->push_back({mirror(value) + " const*", name + "Values"});
                }
                params->push_back({"uint64_t", name + "Count"});
                continue;
            }
            if (hasPrefix(bare, "castl::function") || hasPrefix(bare, "std::function"))
            {
                // 走到这里说明整体形状检查没认出它（例如形参名对不上）—— 仍然拒绝，不许硬发签名。
                *rejectReason = "回调投影：回调参数没有通过形状检查";
                return false;
            }
            // `T&`（无 const）是出参 ⇒ 指针；`T const&` 是入参 ⇒ 走各自的形状分支（别再按指针写）
            const bool isOutRef = spelling.find('&') != std::string::npos &&
                                  spelling.find("const") == std::string::npos;
            if (isScalarType(bare))
            {
                params->push_back({isOutRef ? bare + "*" : (bare == "bool" ? "int32_t" : bare), name});
                continue;
            }
            // 裸指针按不透明指针原样透传（`void*` 就是 `void*`，别拼成 `voidC const*`）
            if (!bare.empty() && bare.back() == '*' || bare == "void")
            {
                params->push_back({spelling.find('*') != std::string::npos ? bareType(spelling) + "*" : "void*", name});
                continue;
            }
            // 枚举按定宽整数投影（矩阵第 1 行）；CXTypeKind 与 params 同序，用规范类型的 kind 认
            if (paramKindFor(method, static_cast<std::size_t>(&param - method.params.data())) ==
                static_cast<int>(CXType_Enum))
            {
                params->push_back({isOutRef ? "int32_t*" : "int32_t", name});
                continue;
            }
            if (spelling.find('&') != std::string::npos && spelling.find("const") == std::string::npos)
            {
                params->push_back({bare + "C*", name + "Out"});
                continue;
            }
            params->push_back({bare + "C const*", name});
        }

        // 返回本类引用（任务 2.2）：C ABI 槽返回 void；托管表面返回同一表面对象（链式）。
        if (method.returnsSelfRef)
        {
            *returnType = "void";
            return true;
        }

        // 资源引用值类的返回位置不在本批支持范围内（只支持 const& 参数过界）。
        if (g_valueTypes.count(bareType(method.returnSpelling)) > 0)
        {
            *rejectReason = "资源引用值类的返回位置不在本批支持范围内（只支持 const& 参数过界）";
            return false;
        }
        // 序列化块记录的返回位置**按值/引用**不在本批支持范围内（本批只定义输入方向）；
        // 返回位置指向它的**裸指针**走 D8 的记录两段式（下面单独一支）。
        if (method.returnSpelling.find('*') == std::string::npos &&
            g_serializedTypes.count(unqualifiedName(bareType(method.returnSpelling))) > 0)
        {
            *rejectReason = "序列化块记录的返回位置不在本批支持范围内（本批只定义输入方向）";
            return false;
        }

        // 静态工厂（任务 2.3）：本批只接"返回纯标量记录"的值记录构造通道。
        // 返回位置含容器 / 文本 / 句柄的工厂在 run() 的镜像收集之前按"返回位置容器"拒绝。
        if (method.isStatic)
        {
            const std::string staticReturn = bareType(method.returnSpelling);
            if (staticReturn.empty() || staticReturn.back() == '*' || isScalarType(staticReturn) ||
                hasPrefix(staticReturn, "castl::vector") || hasPrefix(staticReturn, "std::vector") ||
                hasPrefix(staticReturn, "castl::string") || hasPrefix(staticReturn, "std::string") ||
                hasPrefix(staticReturn, "castl::shared_ptr") || hasPrefix(staticReturn, "std::shared_ptr") ||
                isNameHashSpelling(staticReturn) || isPathHashSpelling(staticReturn))
            {
                *rejectReason = "静态工厂只支持返回纯标量记录（实际 " + staticReturn + "）";
                return false;
            }
            *returnType = shortTypeName(staticReturn) + "C";
            return true;
        }

        const std::string returnBare = bareType(method.returnSpelling);
        // 包装值返回方向不在本 change 内（spec「独立包装值与字节入参必须有确定的值语义」）。
        {
            std::string wrapperInner;
            if (isNameHashSpelling(method.returnSpelling) || isPathHashSpelling(method.returnSpelling) ||
                parseHashObjWrapper(method.returnSpelling, &wrapperInner))
            {
                *rejectReason = "包装值只支持独立入参，不支持返回方向（" + method.returnSpelling + "）";
                return false;
            }
            std::string flagsInner;
            if (parseFlagsWrapper(method.returnSpelling, &flagsInner) ||
                parseFlagsWrapper(method.returnCanonicalSpelling, &flagsInner))
            {
                *rejectReason = "EnumFlags 返回方向不在本 change 的支持范围内（" + method.returnSpelling + "）";
                return false;
            }
        }
        // `void*` 与 `void` 的 bareType 都是 `"void"`，但投影完全不同：
        //   · `void`  → 真的没有返回值
        //   · `void*` → 不透明令牌 `uint64_t`（`GetNativeWindowHandle` 就是这种，旧代码把返回值丢了）
        // 只认 `void*` 这一种拼写；其它 `T*` 返回在下面的兜底里会被拒（见 2.3 的要求）。
        const bool returnIsVoidPointer = returnBare == "void" &&
                                         method.returnSpelling.find('*') != std::string::npos;
        if (returnBare.empty() || (returnBare == "void" && !returnIsVoidPointer))
        {
            *returnType = "void";
        }
        else if (returnIsVoidPointer)
        {
            *returnType = "uint64_t";
        }
        else if (!returnBare.empty() && returnBare.back() != '*' &&
                 method.returnSpelling.find('*') != std::string::npos &&
                 g_bindableInterfaceNames.count(unqualifiedName(returnBare)) > 0)
        {
            // 返回**另一个可绑定接口**的裸指针（`IWindow::GetWindowSystem()` 就是这种，任务 5.3 /
            // design D9）：所有权在模块，本质是借用 —— 投成借用句柄（kind 4），释放只清槽、不销毁。
            // 其它 `T*` 仍按 ② 拒绝：没有"这是借用"的说明就不许静默绑成裸地址。
            *returnType = "uint64_t";
            *isHandleReturn = true;
        }
        else if (!returnBare.empty() && returnBare.back() != '*' &&
                 method.returnSpelling.find('*') != std::string::npos &&
                 g_recordReturnTypes.count(unqualifiedName(returnBare)) > 0)
        {
            // 记录两段式（本批 D8）：返回位置指向**可序列化记录**的非拥有裸指针。
            // C ABI 不发直出槽，发 `Len` / `Copy` 两个伴生（与字符串两段式同线形）：
            // 原生返回空指针 ⇒ Len 返回 0 ⇒ 托管得到 null（不解引用）。
            *returnType = "void";
            *needsTwoStep = true;
            if (twoStepRecordShort != nullptr)
            {
                *twoStepRecordShort = unqualifiedName(returnBare);
            }
            return true;
        }
        else if (!returnBare.empty() && returnBare.back() != '*' &&
                 method.returnSpelling.find('*') != std::string::npos)
        {
            // 所有权不明的裸指针返回（`T*`）：没有登记成可绑定接口时不能静默绑成裸地址；
            // 抽象接口尤其不能生成引用不存在公开类型的产物（任务 2.4 / 3.1）。
            *rejectReason = "返回的裸指针类型 " + returnBare +
                            " 不是构建期已知接口清单里的已绑定接口（未登记抽象接口不得生成悬空公开类型）";
            return false;
        }
        else if (hasPrefix(returnBare, "castl::weak_ptr") || hasPrefix(returnBare, "std::weak_ptr"))
        {
            // 弱句柄（任务 1.4 / design D1）：句柄表里存**弱引用**，不抬高 `use_count()`，
            // 对象析构后 `lock()` 失败 ⇒ 旧句柄按 1001 判失效。旧实现把 `weak_ptr` 塞进 ② 类
            // 整体拒绝，但 ② 的定义是"所有权不明的**裸指针**"，`weak_ptr` 不是裸指针。
            *returnType = "uint64_t";
        }
        else if (hasPrefix(returnBare, "castl::shared_ptr") || hasPrefix(returnBare, "std::shared_ptr") ||
                 hasPrefix(returnBare, "std::unique_ptr"))
        {
            *returnType = "uint64_t";
            *isHandleReturn = true;
        }
        else if (hasPrefix(returnBare, "castl::string_view") || hasPrefix(returnBare, "std::string_view") ||
                 returnBare == "castl::string" || returnBare == "std::string" ||
                 hasPrefix(returnBare, "castl::vector") || hasPrefix(returnBare, "std::vector"))
        {
            *returnType = "void"; // 两段式：由伴生函数拷出
            *needsTwoStep = true;
            // 返回位置的**标量容器**（本批 1.6 / design D13）：`Count` 查条数 + `Copy` 按定长缓冲
            // 逐字节拷；元素限可 blittable 标量/枚举，其它元素（记录 / 句柄 / 文本）维持不可绑，
            // 在这里生成期点名（不静默降级）。
            if (hasPrefix(returnBare, "castl::vector") || hasPrefix(returnBare, "std::vector"))
            {
                const std::size_t open = returnBare.find('<');
                const std::size_t close = returnBare.rfind('>');
                const std::string element =
                    (open != std::string::npos && close != std::string::npos && close > open)
                        ? bareType(trimCopy(returnBare.substr(open + 1, close - open - 1)))
                        : std::string();
                std::string width = normalizeScalarSpelling(element);
                if (width.empty())
                {
                    EnumInfo const* info = findEnumBySpelling(element);
                    if (info != nullptr && isEnumUnderlyingSpelling(info->underlying))
                    {
                        width = info->underlying;
                    }
                }
                if (width.empty())
                {
                    *rejectReason = "返回位置的容器元素 " + element +
                                    " 不是可 blittable 标量/枚举 —— design D13 只开标量容器的两段式；"
                                    "记录容器、含句柄/文本的容器维持不可绑";
                    return false;
                }
                if (twoStepScalarElement != nullptr)
                {
                    *twoStepScalarElement = width;
                }
            }
        }
        else if (method.returnKind == static_cast<int>(CXType_Enum))
        {
            *returnType = "int32_t";
        }
        else if (isScalarType(returnBare))
        {
            *returnType = returnBare == "bool" ? "int32_t" : returnBare;
        }
        else
        {
            *returnType = returnBare + "C";
        }
        return true;
    }

    // ---- 接口编号锁文件（codegen-native-binding-prereqs 任务 1.2）----
    // 编号的唯一来源是入库文件 Tools/CodeGen/interface-ids.lock。默认构建只读它、核对它；
    // 分配新编号只发生在显式 `--update-ids` 模式（append：该段水位 + 1；delete：消失的名字）。
    // 行形状沿用 `*.registry.part`：编号⇥短名⇥完整限定名⇥头文件路径。
    // 段用 `# scope <名字>` 划分，每段一条 `# high water <该段历史最高号>`。
    struct LockEntry
    {
        unsigned number = 0;
        std::string shortName;
        std::string qualifiedName;
        std::string header;
        std::string rawLine;   // 原始行：--update-ids 要求既有行一字不动，重写时原样吐回
    };

    struct LockScope
    {
        std::string name;
        std::vector<LockEntry> entries;
        unsigned highWater = 0;
        bool hasHighWater = false;
    };

    struct InterfaceLock
    {
        std::vector<LockScope> scopes;
    };

    std::string normalizeLockSlashes(std::string value)
    {
        for (char& character : value)
        {
            if (character == '\\')
            {
                character = '/';
            }
        }
        return value;
    }

    // 段起点沿用现值：宿主段从 1、夹具段从 8（spec csharp-bindings 的「段内从该段起点连续」口径）。
    unsigned lockScopeStart(std::string const& scope)
    {
        return scope == "fixture" ? 8u : 1u;
    }

    LockScope* findLockScope(InterfaceLock& lock, std::string const& name)
    {
        for (LockScope& scope : lock.scopes)
        {
            if (scope.name == name)
            {
                return &scope;
            }
        }
        return nullptr;
    }

    // 本批 1.1/2.3：锁条目的身份是**完整限定名**；给了 qualifiedName 时必须两者都匹配，
    // 否则同短名的另一条（别的命名空间）会被误认成同一行。
    LockEntry* findLockEntry(LockScope& scope, std::string const& shortName,
                             std::string const& qualifiedName = std::string())
    {
        for (LockEntry& entry : scope.entries)
        {
            if (entry.shortName == shortName &&
                (qualifiedName.empty() || entry.qualifiedName == qualifiedName))
            {
                return &entry;
            }
        }
        return nullptr;
    }

    LockEntry const* findLockEntry(LockScope const& scope, std::string const& shortName,
                                   std::string const& qualifiedName = std::string())
    {
        for (LockEntry const& entry : scope.entries)
        {
            if (entry.shortName == shortName &&
                (qualifiedName.empty() || entry.qualifiedName == qualifiedName))
            {
                return &entry;
            }
        }
        return nullptr;
    }

    bool parseLockEntry(std::string const& line, LockEntry* entry, std::string* error)
    {
        std::vector<std::string> fields;
        std::size_t begin = 0;
        for (;;)
        {
            const std::size_t tab = line.find('\t', begin);
            fields.push_back(line.substr(begin, tab == std::string::npos ? std::string::npos : tab - begin));
            if (tab == std::string::npos)
            {
                break;
            }
            begin = tab + 1;
        }
        if (fields.size() != 4)
        {
            *error = "锁文件数据行必须是四列（制表符分隔：编号、短名、完整限定名、头文件路径）—— " + line;
            return false;
        }
        char* end = nullptr;
        const unsigned long number = std::strtoul(fields[0].c_str(), &end, 10);
        if (end == fields[0].c_str() || end == nullptr || *end != '\0' || number == 0u)
        {
            *error = "锁文件里的编号不是正整数：" + line;
            return false;
        }
        entry->number = static_cast<unsigned>(number);
        entry->shortName = fields[1];
        entry->qualifiedName = fields[2];
        entry->header = fields[3];
        entry->rawLine = line;
        if (entry->shortName.empty() || entry->qualifiedName.empty() || entry->header.empty())
        {
            *error = "锁文件数据行有空列：" + line;
            return false;
        }
        return true;
    }

    bool loadInterfaceLock(std::string const& path, InterfaceLock* lock, std::string* error)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            *error = "读不到编号锁文件：" + path + "（编号唯一来源是锁文件，缺锁不生成）";
            return false;
        }
        lock->scopes.clear();
        std::string line;
        LockScope* current = nullptr;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (trimCopy(line).empty())
            {
                continue;
            }
            if (line.rfind("# scope ", 0) == 0)
            {
                LockScope scope;
                scope.name = trimCopy(line.substr(8));
                if (scope.name.empty())
                {
                    *error = "锁文件的 `# scope` 行没有段名";
                    return false;
                }
                lock->scopes.push_back(scope);
                current = &lock->scopes.back();
                continue;
            }
            if (line.rfind("# high water ", 0) == 0)
            {
                if (current == nullptr)
                {
                    *error = "锁文件的 `# high water` 出现在任何 `# scope` 之前";
                    return false;
                }
                const unsigned long value = std::strtoul(trimCopy(line.substr(13)).c_str(), nullptr, 10);
                current->highWater = static_cast<unsigned>(value);
                current->hasHighWater = true;
                continue;
            }
            if (line[0] == '#')
            {
                continue;
            }
            if (current == nullptr)
            {
                *error = "锁文件的数据行出现在任何 `# scope` 之前：" + line;
                return false;
            }
            LockEntry entry;
            if (!parseLockEntry(line, &entry, error))
            {
                return false;
            }
            current->entries.push_back(entry);
        }
        if (lock->scopes.empty())
        {
            *error = "锁文件里没有任何 `# scope` 段：" + path;
            return false;
        }
        for (LockScope& scope : lock->scopes)
        {
            if (!scope.hasHighWater)
            {
                *error = "锁文件的段 `" + scope.name + "` 缺 `# high water` 行";
                return false;
            }
            const unsigned start = lockScopeStart(scope.name);
            std::set<unsigned> numbers;
            // 本批 1.1/2.3：段内唯一的是完整限定名（身份），短名只作显示名 —— 同短名可多行。
            std::set<std::string> qualifiedNames;
            unsigned maxNumber = 0;
            for (LockEntry const& entry : scope.entries)
            {
                if (!numbers.insert(entry.number).second)
                {
                    *error = "锁文件的段 `" + scope.name + "` 有重复编号 " + std::to_string(entry.number) +
                             "：" + entry.qualifiedName;
                    return false;
                }
                if (!qualifiedNames.insert(entry.qualifiedName).second)
                {
                    *error = "锁文件的段 `" + scope.name + "` 有重复完整限定名 " + entry.qualifiedName;
                    return false;
                }
                maxNumber = std::max(maxNumber, entry.number);
            }
            if (!numbers.empty())
            {
                if (*numbers.begin() != start)
                {
                    *error = "锁文件的段 `" + scope.name + "` 编号必须从 " + std::to_string(start) +
                             " 起，实际从 " + std::to_string(*numbers.begin()) + " 起";
                    return false;
                }
                unsigned expected = start;
                for (unsigned number : numbers)
                {
                    if (number != expected)
                    {
                        *error = "锁文件的段 `" + scope.name + "` 编号不连续：期望 " +
                                 std::to_string(expected) + "、实际 " + std::to_string(number) +
                                 "（缺号 / 跳号）";
                        return false;
                    }
                    ++expected;
                }
            }
            if (scope.highWater < maxNumber)
            {
                *error = "锁文件的段 `" + scope.name + "` 水位 " + std::to_string(scope.highWater) +
                         " 小于段内最大编号 " + std::to_string(maxNumber);
                return false;
            }
        }
        return true;
    }

    // `--update-ids`：只加锁里没有的新名字行、只删本头已消失的名字行；既有行一字不动；不产绑定。
    // 每个调用处理一个头：`CodeGen <头> --update-ids --lock <锁> --scope <host|fixture> [clang 参数…]`。
    int updateInterfaceIds(const char* filePath, std::string const& lockPath, std::string const& scopeName,
                           const std::vector<const char*>& clangArgs)
    {
        const std::string sourceText = readWholeFile(filePath);
        const std::vector<BoundMarker> markers = scanBoundMarkers(sourceText);
        if (markers.empty())
        {
            std::fprintf(stderr, "CodeGen: --update-ids 在 '%s' 里找不到 CA_BIND 标注\n", filePath);
            return 1;
        }
        // 完整限定名要从 AST 取：文本扫描只有短名，而锁文件的数据行要写完整限定名。
        std::vector<const char*> args;
        args.push_back("-std=c++20");
        args.push_back("-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH");
        for (const char* argument : clangArgs)
        {
            args.push_back(argument);
        }
        CXIndex index = clang_createIndex(1, 0);
        CXTranslationUnit unit = nullptr;
        const CXErrorCode parseError = clang_parseTranslationUnit2(
            index, filePath, args.data(), static_cast<int>(args.size()), nullptr, 0,
            CXTranslationUnit_DetailedPreprocessingRecord, &unit);
        if (parseError != CXError_Success || unit == nullptr)
        {
            std::fprintf(stderr, "CodeGen: --update-ids 解析失败（error=%d）：%s\n",
                         static_cast<int>(parseError), filePath);
            clang_disposeIndex(index);
            return 3;
        }
        unsigned errorCount = 0;
        const unsigned diagnosticCount = clang_getNumDiagnostics(unit);
        for (unsigned i = 0; i < diagnosticCount; ++i)
        {
            CXDiagnostic diagnostic = clang_getDiagnostic(unit, i);
            if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error)
            {
                CXString text = clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions());
                std::fprintf(stderr, "  libclang: %s\n", clang_getCString(text));
                clang_disposeString(text);
                ++errorCount;
            }
            clang_disposeDiagnostic(diagnostic);
        }
        if (errorCount > 0)
        {
            clang_disposeTranslationUnit(unit);
            clang_disposeIndex(index);
            std::fprintf(stderr, "CodeGen: --update-ids 看到 %u 条错误诊断，拒绝改锁。\n", errorCount);
            return 3;
        }
        std::vector<std::string> wanted;
        for (BoundMarker const& marker : markers)
        {
            wanted.push_back(marker.interfaceName);
        }
        std::vector<InterfaceShape> shapes;
        ClassCollector collector;
        collector.wanted = &wanted;
        collector.sink = &shapes;
        clang_visitChildren(clang_getTranslationUnitCursor(unit), collectClasses, &collector);
        clang_disposeTranslationUnit(unit);
        clang_disposeIndex(index);

        std::map<std::string, std::string> qualifiedByName;
        for (InterfaceShape const& shape : shapes)
        {
            qualifiedByName[shape.name] = shape.qualifiedName;
        }
        // 本批 1.1/2.3：发号身份以**完整限定名**为准（短名只是显示名）。同短名不同命名空间
        // 必须各占一行、各自发号；同短名不再被当成"已经登记过"。
        std::set<std::string> markerQualifiedNames;
        for (auto const& entry : qualifiedByName)
        {
            markerQualifiedNames.insert(entry.second);
        }
        for (BoundMarker const& marker : markers)
        {
            if (qualifiedByName.find(marker.interfaceName) == qualifiedByName.end())
            {
                std::fprintf(stderr, "CodeGen: --update-ids 拒绝 —— %s 有 CA_BIND 标注，但找不到类定义\n",
                             marker.interfaceName.c_str());
                return 1;
            }
        }

        // 读原文件的原始行，按段保留注释顺序：只替换目标段的数据行与水位行。
        struct RawBlock
        {
            std::string scope;
            std::vector<std::string> prefix;    // 段头注释（含 `# scope` 行）
            std::vector<std::string> data;      // 原始数据行
            std::string highWaterLine;
        };
        std::vector<std::string> leading;
        std::vector<RawBlock> blocks;
        {
            std::ifstream in(lockPath, std::ios::binary);
            if (!in)
            {
                std::fprintf(stderr, "CodeGen: --update-ids 读不到锁文件：%s\n", lockPath.c_str());
                return 1;
            }
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.rfind("# scope ", 0) == 0)
                {
                    RawBlock block;
                    block.scope = trimCopy(line.substr(8));
                    block.prefix.push_back(line);
                    blocks.push_back(std::move(block));
                    continue;
                }
                if (blocks.empty())
                {
                    leading.push_back(line);
                    continue;
                }
                RawBlock& block = blocks.back();
                if (line.rfind("# high water ", 0) == 0)
                {
                    block.highWaterLine = line;
                }
                else if (!trimCopy(line).empty() && line[0] != '#')
                {
                    block.data.push_back(line);
                }
                else
                {
                    block.prefix.push_back(line);
                }
            }
        }
        RawBlock* target = nullptr;
        for (RawBlock& block : blocks)
        {
            if (block.scope == scopeName)
            {
                target = &block;
                break;
            }
        }
        if (target == nullptr)
        {
            RawBlock block;
            block.scope = scopeName;
            block.prefix.push_back("# scope " + scopeName);
            blocks.push_back(std::move(block));
            target = &blocks.back();
        }
        // 目标段现有条目 + 水位。既有行原样保留（含制表符与顺序）。
        std::vector<LockEntry> existing;
        unsigned highWater = 0;
        {
            for (std::string const& dataLine : target->data)
            {
                LockEntry entry;
                std::string parseErrorText;
                if (!parseLockEntry(dataLine, &entry, &parseErrorText))
                {
                    std::fprintf(stderr, "CodeGen: --update-ids 拒绝 —— %s\n", parseErrorText.c_str());
                    return 1;
                }
                existing.push_back(entry);
            }
            const std::string prefix = "# high water ";
            if (!target->highWaterLine.empty() &&
                target->highWaterLine.rfind(prefix, 0) == 0)
            {
                highWater = static_cast<unsigned>(
                    std::strtoul(trimCopy(target->highWaterLine.substr(prefix.size())).c_str(), nullptr, 10));
            }
            for (LockEntry const& entry : existing)
            {
                highWater = std::max(highWater, entry.number);
            }
        }
        // 名字集合：本头当前标注到的名字。
        std::set<std::string> markerNames;
        for (BoundMarker const& marker : markers)
        {
            markerNames.insert(marker.interfaceName);
        }
        // 别的段里已登记同一个名字 ⇒ 拒绝（段各自独立，但短名在宿主/夹具两段之间也不该重名）。
        {
            InterfaceLock lock;
            std::string lockError;
            if (!loadInterfaceLock(lockPath, &lock, &lockError))
            {
                std::fprintf(stderr, "CodeGen: --update-ids 拒绝 —— %s\n", lockError.c_str());
                return 1;
            }
            for (LockScope const& scope : lock.scopes)
            {
                if (scope.name == scopeName)
                {
                    continue;
                }
                for (LockEntry const& entry : scope.entries)
                {
                    if (markerNames.count(entry.shortName) > 0)
                    {
                        std::fprintf(stderr,
                                     "CodeGen: --update-ids 拒绝 —— %s 已经在段 `%s` 里登记过（编号 %u）\n",
                                     entry.shortName.c_str(), scope.name.c_str(), entry.number);
                        return 1;
                    }
                }
            }
        }
        std::string report;
        std::vector<std::string> keptLines;
        std::set<std::string> keptNames;
        // 本批 1.1：发号身份以完整限定名为准 —— 同短名的另一条（别的命名空间）不算已登记。
        std::set<std::string> keptQualified;
        for (LockEntry const& entry : existing)
        {
            // 删除只针对**本头**的行：`--update-ids` 是逐头调用的（`python build.py --update-ids`
            // 按登记输入逐个跑），按"名字不在本头标注里"删会把别的头的行一起删掉（实测：跑
            // WindowHandle.h 一次，IWindow/桥五条全被删）。别的头的行原样保留。
            const bool sameHeader =
                normalizeLockSlashes(entry.header) == normalizeLockSlashes(filePath);
            if (sameHeader && markerNames.count(entry.shortName) == 0)
            {
                report += "  删除：编号 " + std::to_string(entry.number) + " / " + entry.shortName + "\n";
                continue;
            }
            if (!sameHeader)
            {
                keptLines.push_back(entry.rawLine);
                keptNames.insert(entry.shortName);
                keptQualified.insert(entry.qualifiedName);
                continue;
            }
            auto shape = qualifiedByName.find(entry.shortName);
            if (shape != qualifiedByName.end() && normalizeLockSlashes(shape->second) !=
                                                      normalizeLockSlashes(entry.qualifiedName))
            {
                std::fprintf(stderr,
                             "CodeGen: --update-ids 拒绝 —— %s 的完整限定名变了（锁里 %s、头里 %s）；"
                             "改限定名不是本工具的职责，请人工改锁并评审\n",
                             entry.shortName.c_str(), entry.qualifiedName.c_str(), shape->second.c_str());
                return 1;
            }
            if (normalizeLockSlashes(entry.header) != normalizeLockSlashes(filePath))
            {
                std::fprintf(stderr,
                             "CodeGen: --update-ids 拒绝 —— %s 已登记在别的头（%s），本头是 %s\n",
                             entry.shortName.c_str(), entry.header.c_str(), filePath);
                return 1;
            }
            keptLines.push_back(entry.rawLine);
            keptNames.insert(entry.shortName);
            keptQualified.insert(entry.qualifiedName);
        }
        for (BoundMarker const& marker : markers)
        {
            const std::string markerQualified = qualifiedByName[marker.interfaceName];
            if (keptQualified.count(markerQualified) > 0)
            {
                continue;
            }
            ++highWater;
            const unsigned number = highWater;
            LockEntry entry;
            entry.number = number;
            entry.shortName = marker.interfaceName;
            entry.qualifiedName = qualifiedByName[marker.interfaceName];
            entry.header = normalizeLockSlashes(filePath != nullptr ? filePath : "");
            entry.rawLine = std::to_string(entry.number) + "\t" + entry.shortName + "\t" +
                            entry.qualifiedName + "\t" + entry.header;
            keptLines.push_back(entry.rawLine);
            keptNames.insert(entry.shortName);
            report += "  新增：编号 " + std::to_string(number) + " / " + entry.shortName + "\n";
        }
        target->data = keptLines;
        target->highWaterLine = "# high water " + std::to_string(highWater);

        // 重写：段外行原样，目标段数据行换成 keptLines（既有行原文 + 新行），水位行更新。
        std::ofstream out(lockPath, std::ios::binary);
        if (!out)
        {
            std::fprintf(stderr, "CodeGen: --update-ids 写不了锁文件：%s\n", lockPath.c_str());
            return 2;
        }
        for (std::string const& line : leading)
        {
            out << line << "\n";
        }
        for (RawBlock const& block : blocks)
        {
            for (std::string const& line : block.prefix)
            {
                out << line << "\n";
            }
            for (std::string const& line : block.data)
            {
                out << line << "\n";
            }
            if (!block.highWaterLine.empty())
            {
                out << block.highWaterLine << "\n";
            }
        }
        if (!out)
        {
            std::fprintf(stderr, "CodeGen: --update-ids 写锁文件失败：%s\n", lockPath.c_str());
            return 2;
        }
        std::fprintf(stderr, "CodeGen: --update-ids 完成（段 %s）：\n%s", scopeName.c_str(),
                     report.empty() ? "  无增删\n" : report.c_str());
        return 0;
    }

    // ---- 资源引用值类的解析/发射辅助（任务 3.1；结构定义在 projectMethod 之前）----
    std::string stripSourceComments(std::string const& text)
    {
        std::string stripped;
        stripped.reserve(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            const char current = text[index];
            if (current == '/' && index + 1 < text.size() && text[index + 1] == '/')
            {
                while (index < text.size() && text[index] != '\n')
                {
                    ++index;
                }
                if (index < text.size())
                {
                    stripped.push_back('\n');
                }
                continue;
            }
            if (current == '/' && index + 1 < text.size() && text[index + 1] == '*')
            {
                index += 2;
                while (index + 1 < text.size() && !(text[index] == '*' && text[index + 1] == '/'))
                {
                    ++index;
                }
                index += 1;
                stripped.push_back('\n');
                continue;
            }
            stripped.push_back(current);
        }
        return stripped;
    }

    std::set<std::string> scanValueTypeNames(std::string const& text)
    {
        const std::string stripped = stripSourceComments(text);
        std::set<std::string> names;
        std::size_t position = 0;
        while ((position = stripped.find("CA_BIND_VALUE(", position)) != std::string::npos)
        {
            const std::size_t open = stripped.find('(', position);
            if (open == std::string::npos)
            {
                break;
            }
            const std::size_t close = stripped.find(')', open);
            if (close == std::string::npos)
            {
                break;
            }
            // 宏定义自身也含 `CA_BIND_VALUE(`（`#define CA_BIND_VALUE(TypeName)` 与它的宏体字符串）——
            // 这些是标注机制的名字，不是被标注的类。按"整行以 #define 开头"与"名字必须是标识符"两条滤掉。
            const std::size_t lineBegin = stripped.rfind('\n', position);
            const std::string linePrefix =
                trimCopy(stripped.substr(lineBegin == std::string::npos ? 0 : lineBegin + 1,
                                         position - (lineBegin == std::string::npos ? 0 : lineBegin + 1)));
            const std::string name = trimCopy(stripped.substr(open + 1, close - open - 1));
            bool isIdentifier = !name.empty() &&
                                (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_');
            for (char character : name)
            {
                if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_')
                {
                    isIdentifier = false;
                    break;
                }
            }
            if (isIdentifier && name != "TypeName" && linePrefix.rfind("#define", 0) != 0)
            {
                names.insert(name);
            }
            position = close + 1;
        }
        return names;
    }

    struct ValueCursorCollector
    {
        std::set<std::string> const* wanted = nullptr;
        std::map<std::string, CXCursor>* sink = nullptr;
    };

    CXChildVisitResult collectValueClassCursor(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* collector = static_cast<ValueCursorCollector*>(data);
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if ((kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl) &&
            clang_isCursorDefinition(cursor) != 0)
        {
            const std::string name = toStd(clang_getCursorSpelling(cursor));
            if (collector->wanted->count(name) > 0)
            {
                (*collector->sink)[name] = cursor;
            }
        }
        return CXChildVisit_Recurse;
    }

    struct EnumConstantValueSink
    {
        std::vector<std::pair<std::string, int64_t>>* constants = nullptr;
    };

    CXChildVisitResult collectEnumConstantValues(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* sink = static_cast<EnumConstantValueSink*>(data);
        if (clang_getCursorKind(cursor) == CXCursor_EnumConstantDecl)
        {
            sink->constants->emplace_back(toStd(clang_getCursorSpelling(cursor)),
                                          clang_getEnumConstantDeclValue(cursor));
        }
        return CXChildVisit_Continue;
    }

    unsigned pickKindConstant(std::vector<std::pair<std::string, int64_t>> const& constants,
                              std::initializer_list<char const*> keys)
    {
        for (char const* key : keys)
        {
            for (auto const& entry : constants)
            {
                if (entry.second != 0 && entry.first.find(key) != std::string::npos)
                {
                    return static_cast<unsigned>(entry.second);
                }
            }
        }
        return 0;
    }

    // 取枚举里**精确同名**的常量值 —— 包含 0。`pickKindConstant` 跳过 0 是有意的（"没找到"与
    // "找到了 0" 要能区分），但名称通道恰好落在 `Internal = 0` 上：ImageHandle / BufferHandle 的
    // 枚举都是 Internal=0、对象态在 1/2，跳过 0 会把名称通道错配成另一个对象态常量
    // （实测 ImageHandle 的名字通道被配成 Backbuffer=2，与 WindowHandle 对象通道撞号，
    // 生成的 C++ switch 出现重复 case，Present 也永远拿不到后台缓冲引用）。
    bool exactKindConstant(std::vector<std::pair<std::string, int64_t>> const& constants,
                           std::initializer_list<char const*> keys, unsigned* outValue)
    {
        for (char const* key : keys)
        {
            for (auto const& entry : constants)
            {
                if (entry.first == key && entry.second >= 0)
                {
                    *outValue = static_cast<unsigned>(entry.second);
                    return true;
                }
            }
        }
        return false;
    }

    unsigned firstOtherConstant(std::vector<std::pair<std::string, int64_t>> const& constants,
                                unsigned taken)
    {
        for (auto const& entry : constants)
        {
            if (entry.second != 0 && static_cast<unsigned>(entry.second) != taken)
            {
                return static_cast<unsigned>(entry.second);
            }
        }
        return 0;
    }

    std::string stripInterfacePrefix(std::string const& name)
    {
        if (name.size() > 1 && name[0] == 'I' && std::isupper(static_cast<unsigned char>(name[1])))
        {
            return name.substr(1);
        }
        return name;
    }

    // 值类的 kind 枚举从**源码文本**里取：libclang 对嵌套 enum 的访问在实测中不可靠
    // （同样的类体里 EnumDecl 子节点取不到），而枚举常量本身就是源码里逐字写着的。
    bool findClassBodyText(std::string const& source, std::string const& className, std::string* body)
    {
        const std::string stripped = stripSourceComments(source);
        std::size_t position = stripped.find("class " + className);
        if (position == std::string::npos)
        {
            position = stripped.find("struct " + className);
        }
        if (position == std::string::npos)
        {
            return false;
        }
        const std::size_t brace = stripped.find('{', position);
        if (brace == std::string::npos)
        {
            return false;
        }
        int depth = 0;
        for (std::size_t index = brace; index < stripped.size(); ++index)
        {
            if (stripped[index] == '{')
            {
                ++depth;
            }
            else if (stripped[index] == '}')
            {
                --depth;
                if (depth == 0)
                {
                    *body = stripped.substr(brace + 1, index - brace - 1);
                    return true;
                }
            }
        }
        return false;
    }

    // 类体可能在**被包含的头**里（句柄值类与元数据记录的真实定义处）：主头里找不到时，
    // 读该游标所在的声明文件再找一次。不这样做的话，`CA_BIND_VALUE(ImageHandle)` 与
    // `FindShaderStructData` 的记录都会在"源码里找不到类体"这一步被拒。
    bool findClassBodyTextNearCursor(CXCursor cursor, std::string const& className,
                                     std::string const& mainText, std::string* body)
    {
        if (findClassBodyText(mainText, className, body))
        {
            return true;
        }
        CXFile file = nullptr;
        unsigned line = 0;
        unsigned column = 0;
        unsigned offset = 0;
        clang_getSpellingLocation(clang_getCursorLocation(cursor), &file, &line, &column, &offset);
        if (file == nullptr)
        {
            return false;
        }
        const std::string path = toStd(clang_getFileName(file));
        if (path.empty())
        {
            return false;
        }
        const std::string text = readWholeFile(path.c_str());
        return !text.empty() && findClassBodyText(text, className, body);
    }

    bool parseEnumConstantsFromText(std::string const& classBody, std::vector<std::pair<std::string, int64_t>>* constants,
                                    std::string* enumShort)
    {
        const std::size_t enumAt = classBody.find("enum ");
        if (enumAt == std::string::npos)
        {
            return false;
        }
        std::size_t cursor = enumAt + 5;
        // `enum class <名字>` / `enum <名字>`
        while (cursor < classBody.size() && std::isspace(static_cast<unsigned char>(classBody[cursor])))
        {
            ++cursor;
        }
        if (classBody.compare(cursor, 5, "class") == 0)
        {
            cursor += 5;
            while (cursor < classBody.size() && std::isspace(static_cast<unsigned char>(classBody[cursor])))
            {
                ++cursor;
            }
        }
        const std::size_t nameBegin = cursor;
        while (cursor < classBody.size() &&
               (std::isalnum(static_cast<unsigned char>(classBody[cursor])) || classBody[cursor] == '_'))
        {
            ++cursor;
        }
        *enumShort = classBody.substr(nameBegin, cursor - nameBegin);
        const std::size_t open = classBody.find('{', cursor);
        const std::size_t close = open == std::string::npos ? std::string::npos : classBody.find('}', open);
        if (open == std::string::npos || close == std::string::npos || close <= open)
        {
            return false;
        }
        int64_t nextValue = 0;
        for (std::string const& raw : splitTopLevelArguments(classBody.substr(open + 1, close - open - 1)))
        {
            const std::string text = trimCopy(raw);
            if (text.empty())
            {
                continue;
            }
            const std::size_t equals = text.find('=');
            std::string name = trimCopy(equals == std::string::npos ? text : text.substr(0, equals));
            if (name.empty())
            {
                continue;
            }
            int64_t value = nextValue;
            if (equals != std::string::npos)
            {
                const std::string valueText = trimCopy(text.substr(equals + 1));
                value = static_cast<int64_t>(std::strtoll(valueText.c_str(), nullptr, 10));
            }
            constants->emplace_back(name, value);
            nextValue = value + 1;
        }
        return !constants->empty();
    }

    bool buildValueTypeInfo(CXCursor cursor, std::string const& classBody, ValueTypeInfo* info,
                            std::string* whyNot)
    {
        info->shortName = toStd(clang_getCursorSpelling(cursor));
        info->qualifiedName = qualifiedCursorName(cursor);
        std::vector<std::pair<std::string, int64_t>> constants;
        struct ChildState
        {
            ValueTypeInfo* info = nullptr;
        } state{ info };
        clang_visitChildren(
            cursor,
            [](CXCursor child, CXCursor, CXClientData data) {
                auto* state = static_cast<ChildState*>(data);
                if (clang_getCursorKind(child) != CXCursor_Constructor)
                {
                    return CXChildVisit_Continue;
                }
                if (clang_getCXXAccessSpecifier(child) == CX_CXXPrivate ||
                    clang_getCXXAccessSpecifier(child) == CX_CXXProtected)
                {
                    return CXChildVisit_Continue;
                }
                const int argumentCount = clang_Cursor_getNumArguments(child);
                // 通道构造也可以带一个**带默认值**的尾部 bool（`ImageHandle(NameHash const&, bool unique = false)`）：
                // 重建调用只传第一个参数，尾部 bool 走默认值。第二个参数不是 bool 的构造不认。
                if (argumentCount < 1 || argumentCount > 2)
                {
                    return CXChildVisit_Continue;
                }
                if (argumentCount == 2)
                {
                    const CXType secondType = clang_getCanonicalType(
                        clang_getCursorType(clang_Cursor_getArgument(child, 1)));
                    if (secondType.kind != CXType_Bool)
                    {
                        return CXChildVisit_Continue;
                    }
                }
                const CXCursor argument = clang_Cursor_getArgument(child, 0);
                CXType argumentType = clang_getCanonicalType(clang_getCursorType(argument));
                const std::string spelling = toStd(clang_getTypeSpelling(argumentType));
                const std::string bare = bareType(spelling);
                if (hasPrefix(bare, "castl::shared_ptr") || hasPrefix(bare, "std::shared_ptr"))
                {
                    const std::size_t open = bare.find('<');
                    const std::size_t close = bare.rfind('>');
                    if (open != std::string::npos && close != std::string::npos && close > open)
                    {
                        const std::string inner = trimCopy(bare.substr(open + 1, close - open - 1));
                        const std::string innerShort = unqualifiedName(bareType(inner));
                        const std::string knownQualified =
                            knownInterfaceQualified(innerShort, state->info->qualifiedName);
                        if (!knownQualified.empty())
                        {
                            // 逐条通道都记下来（design D12：ImageHandle 有 GPUTexture / WindowHandle
                            // 两条对象构造通道）；首个通道同时写进旧的单数字段，既有调用点不动。
                            ValueTypeInfo::ObjectChannel channel;
                            channel.pointeeShort = innerShort;
                            channel.pointeeQualified = knownQualified;
                            channel.factoryName = "From" + stripInterfacePrefix(innerShort);
                            state->info->objectChannels.push_back(channel);
                            if (state->info->objectPointeeQualified.empty())
                            {
                                state->info->objectPointeeShort = innerShort;
                                state->info->objectPointeeQualified = knownQualified;
                                state->info->objectFactoryName = channel.factoryName;
                            }
                        }
                    }
                    return CXChildVisit_Continue;
                }
                if (isNameHashSpelling(bare) || isPathHashSpelling(bare))
                {
                    state->info->hasNameCtor = true;
                }
                return CXChildVisit_Continue;
            },
            &state);
        if (!parseEnumConstantsFromText(classBody, &constants, &info->enumShort))
        {
            *whyNot = "值类 " + info->shortName + " 没有可用的 kind 枚举（托管工厂的 kind 无从投影）";
            return false;
        }
        info->objectKind = pickKindConstant(constants, { "Object", "External", "Backbuffer" });
        // 名称通道 = 枚举里精确的 `Internal`（值 0）。这里不能用 `pickKindConstant`：它跳过 0，
        // 会把名称通道错配到另一个对象态常量上（实测 ImageHandle 错配成 Backbuffer=2，与
        // WindowHandle 对象通道撞号 ⇒ 生成的 switch 重复 case、Present 拿不到后台缓冲引用）。
        unsigned exactNamedKind = 0;
        const bool hasExactNamedKind =
            exactKindConstant(constants, { "Internal", "Named", "Name" }, &exactNamedKind);
        info->nameKind = hasExactNamedKind ? exactNamedKind : 0;
        info->hasInvalidKind = exactKindConstant(constants, { "Invalid" }, &info->invalidKind);
        // 多条对象通道的 kind 按"枚举里对象态的常量顺序"配（ImageType：External / Backbuffer；
        // 构造声明顺序与它们一致，实测 ImageHandle 的 GPUTexture 构造写 External、WindowHandle 写
        // Backbuffer）。通道数超过候选常量数就点名拒绝，不猜。
        if (!info->objectChannels.empty())
        {
            std::vector<int64_t> objectKinds;
            for (char const* preferred : { "Object", "External", "Backbuffer" })
            {
                for (auto const& constant : constants)
                {
                    if (constant.first == preferred)
                    {
                        objectKinds.push_back(constant.second);
                        break;
                    }
                }
            }
            if (info->objectChannels.size() > objectKinds.size())
            {
                *whyNot = "值类 " + info->shortName + " 的对象构造通道有 " +
                          std::to_string(info->objectChannels.size()) +
                          " 条，但 kind 枚举里只有 " + std::to_string(objectKinds.size()) +
                          " 个对象态常量（Object/External/Backbuffer）—— 不猜 kind";
                return false;
            }
            for (std::size_t index = 0; index < info->objectChannels.size(); ++index)
            {
                info->objectChannels[index].kind = static_cast<unsigned>(objectKinds[index]);
            }
            info->objectKind = info->objectChannels.front().kind;
        }
        if (info->hasNameCtor && !hasExactNamedKind)
        {
            *whyNot = "值类 " + info->shortName +
                      " 有名称构造通道，但 kind 枚举里没有 `Internal`/`Named`/`Name` 常量 —— 不猜 kind";
            return false;
        }
        if (info->objectKind == 0 && !info->objectPointeeQualified.empty())
        {
            info->objectKind = firstOtherConstant(constants, info->nameKind);
        }
        if (info->objectPointeeQualified.empty() && !info->hasNameCtor)
        {
            *whyNot = "值类 " + info->shortName +
                      " 没有可投影的构造通道（本批支持 shared_ptr<已绑定接口> 与 NameHash/PathHash 两条）";
            return false;
        }
        if ((!info->objectPointeeQualified.empty() && info->objectKind == 0) ||
            (info->hasNameCtor && !hasExactNamedKind))
        {
            *whyNot = "值类 " + info->shortName + " 的 kind 枚举里找不到构造通道对应的常量值";
            return false;
        }
        return true;
    }

    std::string emitValueTypeCSharp(ValueTypeInfo const& info)
    {
        std::string cs;
        cs += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
        cs += "// 共享值类型（资源引用）：只作参数过界，自身成员方法不进表面。\n";
        cs += "// 限定名：" + info.qualifiedName + "\n";
        cs += "using System;\n\n";
        cs += "namespace " + cSharpNamespaceOf(info.qualifiedName) + "\n{\n";
        cs += "    /// <summary>资源引用值类：kind + 对象句柄 / 名称两态，托管工厂与构造通道一一对应</summary>\n";
        cs += "    public sealed class " + info.shortName + "\n    {\n";
        cs += "        internal readonly uint Kind;\n";
        cs += "        internal readonly ulong Handle;\n";
        cs += "        internal readonly string Name;\n";
        cs += "        internal " + info.shortName + "(uint kind, ulong handle, string name)\n        {\n";
        cs += "            Kind = kind;\n            Handle = handle;\n            Name = name;\n        }\n";
        if (!info.objectChannels.empty())
        {
            // 每条对象构造通道一个工厂（design D12：ImageHandle 的 GPUTexture / WindowHandle 两条）。
            for (ValueTypeInfo::ObjectChannel const& channel : info.objectChannels)
            {
                cs += "        /// <summary>按已绑定对象构造：句柄来自目标表面对象的租约（同一对象性由原生侧反查保证）</summary>\n";
                cs += "        public static " + info.shortName + " " + channel.factoryName + "(" +
                      channel.pointeeShort + " value)\n        {\n";
                cs += "            return new " + info.shortName + "(" + std::to_string(channel.kind) +
                      "u, (ulong)(long)value.Token, string.Empty);\n        }\n";
            }
        }
        if (info.hasNameCtor)
        {
            cs += "        /// <summary>按名称构造：原生侧用 NameHash 原生构造通道重建</summary>\n";
            cs += "        public static " + info.shortName + " " + info.nameFactoryName +
                  "(string name)\n        {\n";
            cs += "            return new " + info.shortName + "(" + std::to_string(info.nameKind) +
                  "u, 0UL, name ?? string.Empty);\n        }\n";
        }
        if (info.hasInvalidKind)
        {
            cs += "        /// <summary>无效值（枚举的 Invalid 态）：记录字段的\"未设置\"用它过界</summary>\n";
            cs += "        public static " + info.shortName + " Invalid => new " + info.shortName +
                  "(" + std::to_string(info.invalidKind) + "u, 0UL, string.Empty);\n";
        }
        cs += "    }\n}\n";
        return cs;
    }

    // ---- 序列化块记录（任务 3.2）的字段收集 / 编解码发射 ----
    uint64_t fnv1a64(std::string const& text)
    {
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char byte : text)
        {
            hash ^= static_cast<uint64_t>(byte);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::string normalizedScalarType(CXType type)
    {
        const CXType canonical = clang_getCanonicalType(type);
        if (canonical.kind == CXType_Bool)
        {
            return "bool";
        }
        if (canonical.kind == CXType_Float)
        {
            return "float";
        }
        if (canonical.kind == CXType_Double)
        {
            return "double";
        }
        const long long size = clang_Type_getSizeOf(canonical);
        bool isSigned = false;
        switch (canonical.kind)
        {
        case CXType_Char_S:
        case CXType_SChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong:
            isSigned = true;
            break;
        default:
            isSigned = false;
            break;
        }
        if (size == 1) return isSigned ? "int8_t" : "uint8_t";
        if (size == 2) return isSigned ? "int16_t" : "uint16_t";
        if (size == 4) return isSigned ? "int32_t" : "uint32_t";
        if (size == 8) return isSigned ? "int64_t" : "uint64_t";
        return std::string();
    }

    std::string csharpScalarTypeForWidth(std::string const& width)
    {
        if (width == "bool") return "bool";
        if (width == "float") return "float";
        if (width == "double") return "double";
        return cSharpIntegerType(width);
    }

    // BinaryReader 的方法名是 `ReadUInt32` / `ReadInt16` / `ReadSingle` …，不是 `Read` + C# 类型名
    // （照拼会得到 `Readuint`）。标量字段的读入口按宽度类型映射到这里。
    std::string csharpReaderCall(std::string const& width)
    {
        if (width == "bool") return "reader.ReadByte() != 0";
        if (width == "uint8_t" || width == "unsigned char") return "reader.ReadByte()";
        if (width == "int8_t" || width == "signed char") return "(sbyte)reader.ReadByte()";
        if (width == "uint16_t" || width == "unsigned short") return "reader.ReadUInt16()";
        if (width == "int16_t" || width == "short") return "reader.ReadInt16()";
        if (width == "uint32_t" || width == "unsigned int" || width == "unsigned") return "reader.ReadUInt32()";
        if (width == "int32_t" || width == "int") return "reader.ReadInt32()";
        if (width == "uint64_t" || width == "unsigned long" || width == "size_t") return "reader.ReadUInt64()";
        if (width == "int64_t" || width == "long") return "reader.ReadInt64()";
        if (width == "float") return "reader.ReadSingle()";
        if (width == "double") return "reader.ReadDouble()";
        return "reader.ReadInt32()";
    }

    bool buildSerializedElementFields(CXType elementType, SerializedField* field,
                                      std::string* layoutText, std::string* error);
    bool buildSerializedFieldList(CXCursor recordCursor, std::vector<SerializedField>* fields,
                                  std::string* layoutText, std::string const& indent, std::string* error);
    bool buildSerializedField(CXCursor recordCursor, CXCursor fieldCursor, SerializedField* field,
                              std::string* layoutText, std::string* error)
    {
        field->name = toStd(clang_getCursorSpelling(fieldCursor));
        const CXType fieldType = clang_getCursorType(fieldCursor);
        CXType canonical = clang_getCanonicalType(fieldType);
        field->cppType = toStd(clang_getTypeSpelling(canonical));
        field->offsetBytes =
            static_cast<uint64_t>(clang_Type_getOffsetOf(clang_getCursorType(recordCursor),
                                                         field->name.c_str()) / 8);
        const std::string bare = bareType(field->cppType);
        if (isScalarType(bare))
        {
            field->kind = 0;
            field->widthType = normalizedScalarType(canonical);
            if (field->widthType.empty())
            {
                *error = "字段 " + field->name + " 的标量宽度认不出来（" + field->cppType + "）";
                return false;
            }
        }
        else if (canonical.kind == CXType_Enum)
        {
            field->kind = 1;
            EnumInfo const* info = findEnumBySpelling(field->cppType);
            if (info == nullptr)
            {
                info = findEnumBySpelling(toStd(clang_getTypeSpelling(canonical)));
            }
            if (info == nullptr || !isEnumUnderlyingSpelling(info->underlying))
            {
                *error = "字段 " + field->name + " 的枚举底层类型认不出来（" + field->cppType + "）";
                return false;
            }
            field->widthType = info->underlying;
            field->enumShort = info->shortName;
        }
        else if (isNameHashSpelling(field->cppType))
        {
            field->kind = 2;
            field->textIsNameHash = true;
        }
        // 联合体（真实形状：`union GraphicsClearValue { float color[4]; struct {...} depthStencil; }`）：
        // 没有可逐字段投影的成员名，按**定长原样字节**过界（C# 镜像里是 `byte[]`）。
        // 只允许出现在序列化块记录（class 镜像）里；元素记录（struct 镜像）里出现就点名拒绝。
        else if (canonical.kind == CXType_Record &&
                 clang_getCursorKind(clang_getTypeDeclaration(canonical)) == CXCursor_UnionDecl)
        {
            const long long size = clang_Type_getSizeOf(canonical);
            if (size <= 0 || size > 64)
            {
                *error = "字段 " + field->name + " 的联合体大小不可用或超出本批上限（" +
                         std::to_string(size) + " 字节）";
                return false;
            }
            field->kind = 10;
            field->blobSize = static_cast<std::size_t>(size);
        }
        // 值类字段（D11 ①的标量形态：真实形状 `BufferHandle m_Buffer` / `ImageHandle m_Color`）：
        // 过界就是 kind + 句柄 + 名称三段线形，按绑定对象表反查重建。
        else if (g_valueTypes.count(unqualifiedName(bare)) > 0)
        {
            field->kind = 9;
            field->valueClassShort = unqualifiedName(bare);
        }
        // 枚举位标志包装（真实形状：`uenum::EnumFlags<EColorChannelMask> channelMask`）：规范类型是
        // 记录（一个带 `m_Value` 的壳），但它过界就是底层整数 + 枚举类型名。
        else if (hasPrefix(bare, "uenum::EnumFlags<") || hasPrefix(bare, "castl::EnumFlags<") ||
                 hasPrefix(bare, "std::EnumFlags<"))
        {
            const std::size_t open = bare.find('<');
            const std::size_t close = bare.rfind('>');
            const std::string inner = (open != std::string::npos && close != std::string::npos && close > open)
                                          ? bareType(trimCopy(bare.substr(open + 1, close - open - 1)))
                                          : std::string();
            const EnumInfo* info = findEnumBySpelling(inner);
            if (info == nullptr || !isEnumUnderlyingSpelling(info->underlying))
            {
                *error = "字段 " + field->name + " 的枚举位标志内层枚举认不出来（" + inner + "）";
                return false;
            }
            field->kind = 1;
            field->widthType = info->underlying;
            field->enumShort = info->shortName;
        }
        else if (isPathHashSpelling(field->cppType))
        {
            field->kind = 2;
            field->textIsPathHash = true;
        }
        else if (bare == "castl::string" || bare == "std::string" ||
                 hasPrefix(bare, "std::basic_string<char") || hasPrefix(bare, "castl::basic_string<char"))
        {
            // 规范拼写：`castl::string` 就是 `std::basic_string<char>`，某些字段的类型拼写会落成
            // 后者（实测 ComputeBatch::kernelName）。两者都必须按文本字段过界。
            field->kind = 2;
        }
        else if (hasPrefix(bare, "castl::string_view") || hasPrefix(bare, "std::string_view") ||
                 hasPrefix(bare, "castl::basic_string_view") || hasPrefix(bare, "std::basic_string_view"))
        {
            *error = "字段 " + field->name + " 是 string_view —— 反序列化出来的视图会悬空；"
                     "本批文本字段用 castl::string / NameHash / PathHash";
            return false;
        }
        else if (hasPrefix(bare, "castl::vector") || hasPrefix(bare, "std::vector"))
        {
            const std::size_t open = bare.find('<');
            const std::size_t close = bare.rfind('>');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                *error = "字段 " + field->name + " 的容器类型拼写不完整（" + field->cppType + "）";
                return false;
            }
            const std::string element = trimCopy(bare.substr(open + 1, close - open - 1));
            const std::string elementBare = bareType(element);
            field->kind = 3;
            if (hasPrefix(elementBare, "castl::vector") || hasPrefix(elementBare, "std::vector"))
            {
                *error = "字段 " + field->name + " 是两层容器（vector<vector<T>>）—— 超出本批"
                         "实现覆盖界（首批见证形状全是一层 vector）；恢复路径：真实实例出现时立扩形状批";
                return false;
            }
            CXType elementType = clang_Type_getTemplateArgumentAsType(canonical, 0);
            if (elementType.kind == CXType_Invalid)
            {
                *error = "字段 " + field->name + " 的容器元素类型无法从模板实参解析（" + field->cppType +
                         "）；本批支持一层 vector<标量|枚举|纯标量记录>";
                return false;
            }
            const CXType elementCanonical = clang_getCanonicalType(elementType);
            const std::string elementSpelling = bareType(toStd(clang_getTypeSpelling(elementCanonical)));
            if (isScalarType(elementSpelling))
            {
                field->elementKind = 0;
                field->elementWidthType = normalizedScalarType(elementCanonical);
                if (field->elementWidthType.empty())
                {
                    *error = "字段 " + field->name + " 的容器元素标量宽度认不出来（" + elementSpelling + "）";
                    return false;
                }
            }
            else if (elementCanonical.kind == CXType_Enum)
            {
                field->elementKind = 1;
                EnumInfo const* info = findEnumBySpelling(elementSpelling);
                if (info == nullptr)
                {
                    info = findEnumBySpelling(toStd(clang_getTypeSpelling(elementCanonical)));
                }
                if (info == nullptr || !isEnumUnderlyingSpelling(info->underlying))
                {
                    *error = "字段 " + field->name + " 的容器元素枚举底层类型认不出来（" +
                             elementSpelling + "）";
                    return false;
                }
                field->elementWidthType = info->underlying;
                field->elementEnumShort = info->shortName;
            }
            else if (isNameHashSpelling(elementSpelling) || isPathHashSpelling(elementSpelling) ||
                     elementSpelling == "castl::string" || elementSpelling == "std::string" ||
                     hasPrefix(elementSpelling, "std::basic_string<char") ||
                     hasPrefix(elementSpelling, "castl::basic_string<char"))
            {
                // 文本元素（本批 D8 扩展）：元数据记录里的 `vector<NameHash>`（纹理采样器名字表）
                // 就是这种形状。过界与标量 string 字段同线形：u64 长度 + UTF-8 字节。
                // 必须排在"记录元素"之前：`NameHash` 的规范类型也是 record，先判记录会去拆它的
                // 内部字段（实测报 "字段 m_NameView 的嵌套记录没有字段（std::basic_string_view<char>）"）。
                field->elementKind = 3;
                field->elementTextIsNameHash = isNameHashSpelling(elementSpelling);
                field->elementTextIsPathHash = isPathHashSpelling(elementSpelling);
            }
            else if (elementCanonical.kind == CXType_Record &&
                     g_valueTypes.count(unqualifiedName(elementSpelling)) > 0)
            {
                // D11 ①：值类容器元素（真实形状：`vector<ImageHandle> m_Attachments`）。
                field->elementKind = 4;
                field->elementValueClassShort = unqualifiedName(elementSpelling);
            }
            else if (elementCanonical.kind == CXType_Record)
            {
                field->elementKind = 2;
                field->elementRecordShort = unqualifiedName(elementSpelling);
                if (!buildSerializedElementFields(elementCanonical, field, layoutText, error))
                {
                    return false;
                }
            }
            else if (elementSpelling.find("unordered_map") != std::string::npos ||
                     elementSpelling.find("std::map") != std::string::npos ||
                     elementSpelling.find("std::set") != std::string::npos)
            {
                *error = "字段 " + field->name + " 的元素是映射/集合容器 —— 本批未实现该形状；"
                         "恢复路径：真实实例出现时立扩形状批";
                return false;
            }
            else
            {
                *error = "字段 " + field->name + " 的容器元素形状不在本批支持范围内（" + elementSpelling +
                         "）；本批支持一层 vector<标量|枚举|纯标量记录>";
                return false;
            }
        }
        // 定长数组成员（D11 第四类元素：真实形状 `std::array<SingleColorAttachmentBlendStates, 8>
        // m_AttachmentBlendStates`）：**不带 count**，元素个数是类型的一部分；元素按记录逐字段线形。
        // 只支持"纯标量字段的记录元素"（有容器/文本的元素记录由 buildSerializedElementFields 点名拒绝）。
        else if (hasPrefix(bare, "std::array<") || hasPrefix(bare, "castl::array<"))
        {
            const std::size_t open = bare.find('<');
            const std::size_t close = bare.rfind('>');
            const std::string inside = (open != std::string::npos && close != std::string::npos && close > open)
                                           ? trimCopy(bare.substr(open + 1, close - open - 1))
                                           : std::string();
            const std::size_t comma = inside.rfind(',');
            if (comma == std::string::npos)
            {
                *error = "字段 " + field->name + " 的定长数组拼写里找不到元素个数（" + field->cppType + "）";
                return false;
            }
            const std::string countText = trimCopy(inside.substr(comma + 1));
            std::size_t fixedCount = 0;
            try
            {
                fixedCount = static_cast<std::size_t>(std::stoul(countText));
            }
            catch (...)
            {
                fixedCount = 0;
            }
            if (fixedCount == 0)
            {
                *error = "字段 " + field->name + " 的定长数组元素个数认不出来（" + countText + "）";
                return false;
            }
            CXType elementType = clang_Type_getTemplateArgumentAsType(canonical, 0);
            if (elementType.kind == CXType_Invalid)
            {
                *error = "字段 " + field->name + " 的定长数组元素类型无法从模板实参解析（" + field->cppType + "）";
                return false;
            }
            const CXType elementCanonical = clang_getCanonicalType(elementType);
            const std::string elementSpelling = bareType(toStd(clang_getTypeSpelling(elementCanonical)));
            if (elementCanonical.kind != CXType_Record)
            {
                *error = "字段 " + field->name + " 的定长数组元素不是记录（" + elementSpelling +
                         "）—— 本批只定义记录元素的定长数组";
                return false;
            }
            field->kind = 8;
            field->fixedCount = fixedCount;
            field->elementKind = 2;
            field->elementRecordShort = unqualifiedName(elementSpelling);
            field->elementRecordQualified = qualifiedCursorName(clang_getTypeDeclaration(elementCanonical));
            if (!buildSerializedElementFields(elementCanonical, field, layoutText, error))
            {
                return false;
            }
        }
        // 包装成员（D11 附）：`cacore::HashObj<T>`（真实形状：`HashObj<ViewRectData> m_ViewPort`）。
        // 线形 = 内层记录的字段逐项；读回时重新包成 HashObj。
        // 外层条件排除映射 / 集合 / 定长数组 / 智能指针：它们的规范类型也是 record，必须落到各自的分支。
        else if (canonical.kind == CXType_Record &&
                 !hasPrefix(bare, "castl::unordered_map") && !hasPrefix(bare, "std::unordered_map") &&
                 !hasPrefix(bare, "castl::map") && !hasPrefix(bare, "std::map") &&
                 !hasPrefix(bare, "castl::set") && !hasPrefix(bare, "std::set") &&
                 !hasPrefix(bare, "castl::shared_ptr") && !hasPrefix(bare, "std::shared_ptr") &&
                 !hasPrefix(bare, "castl::weak_ptr") && !hasPrefix(bare, "std::weak_ptr") &&
                 bare.find("std::array") == std::string::npos &&
                 bare.find("castl::array") == std::string::npos)
        {
            std::string hashInner;
            if (parseHashObjWrapper(field->cppType, &hashInner) ||
                parseHashObjWrapper(toStd(clang_getTypeSpelling(canonical)), &hashInner))
            {
                // HashObj<T> 的模板实参就是内层记录类型：直接从模板实参取声明，不靠全局表。
                CXType innerType = clang_Type_getTemplateArgumentAsType(canonical, 0);
                CXCursor declaration = clang_getTypeDeclaration(clang_getCanonicalType(innerType));
                if (clang_Cursor_isNull(declaration))
                {
                    *error = "字段 " + field->name + " 的 HashObj 内层记录 " + hashInner + " 找不到定义";
                    return false;
                }
                field->kind = 7;
                field->isHashObjWrapper = true;
                field->nestedRecordShort = unqualifiedName(bareType(hashInner));
                field->nestedRecordQualified = qualifiedCursorName(declaration);
                if (!buildSerializedFieldList(declaration, &field->nestedFields, &field->nestedLayoutText,
                                              "  ", error))
                {
                    return false;
                }
                if (field->nestedFields.empty())
                {
                    *error = "字段 " + field->name + " 的 HashObj 内层记录没有字段";
                    return false;
                }
            }
            else if (!hasPrefix(bare, "castl::unordered_map") && !hasPrefix(bare, "std::unordered_map") &&
                     !hasPrefix(bare, "castl::map") && !hasPrefix(bare, "std::map") &&
                     !hasPrefix(bare, "castl::set") && !hasPrefix(bare, "std::set") &&
                     !hasPrefix(bare, "castl::shared_ptr") && !hasPrefix(bare, "std::shared_ptr") &&
                     !hasPrefix(bare, "castl::weak_ptr") && !hasPrefix(bare, "std::weak_ptr") &&
                     bare.find("std::array") == std::string::npos &&
                     bare.find("castl::array") == std::string::npos)
            {
                CXCursor declaration = clang_getTypeDeclaration(canonical);
                if (clang_Cursor_isNull(declaration) || clang_isCursorDefinition(declaration) == 0)
                {
                    *error = "字段 " + field->name + " 的记录类型找不到定义（" + field->cppType + "）";
                    return false;
                }
                field->kind = 4;
                field->nestedRecordShort = unqualifiedName(field->cppType);
                field->nestedRecordQualified = qualifiedCursorName(declaration);
                // 嵌套记录自己的字段清单与布局文本单独攒，父层按"父字段行 + 嵌套行"的顺序拼。
                if (!buildSerializedFieldList(declaration, &field->nestedFields, &field->nestedLayoutText,
                                              "  ", error))
                {
                    return false;
                }
                if (field->nestedFields.empty())
                {
                    *error = "字段 " + field->name + " 的嵌套记录没有字段（" + field->cppType + "）";
                    return false;
                }
            }
            else
            {
                *error = "字段 " + field->name + " 是映射容器 / 定长数组（" + field->cppType +
                         "）—— 本批未实现该形状；恢复路径：真实实例出现时立扩形状批";
                return false;
            }
        }
        // 命名映射成员（D11 ②）：键固定 `cacore::NameHash`，值限标量 / 枚举 / 记录 / 值类 / 接口引用。
        else if (hasPrefix(bare, "castl::unordered_map") || hasPrefix(bare, "std::unordered_map") ||
                 hasPrefix(bare, "castl::map") || hasPrefix(bare, "std::map"))
        {
            CXType keyType = clang_Type_getTemplateArgumentAsType(canonical, 0);
            CXType valueType = clang_Type_getTemplateArgumentAsType(canonical, 1);
            if (keyType.kind == CXType_Invalid || valueType.kind == CXType_Invalid)
            {
                *error = "字段 " + field->name + " 的映射类型实参解析不出来（" + field->cppType + "）";
                return false;
            }
            const std::string keySpelling =
                bareType(toStd(clang_getTypeSpelling(clang_getCanonicalType(keyType))));
            if (!isNameHashSpelling(keySpelling))
            {
                *error = "字段 " + field->name + " 的映射容器键不是 cacore::NameHash（实际 " + keySpelling +
                         "）—— design D11 只定义 NameHash 键的命名映射；键非 NameHash 的映射容器维持不可绑"
                         "（不静默丢字段）";
                return false;
            }
            field->kind = 5;
            const CXType valueCanonical = clang_getCanonicalType(valueType);
            const std::string valueSpelling = bareType(toStd(clang_getTypeSpelling(valueCanonical)));
            if (isScalarType(valueSpelling))
            {
                field->mapValueKind = 0;
                field->mapValueWidthType = normalizedScalarType(valueCanonical);
                if (field->mapValueWidthType.empty())
                {
                    *error = "字段 " + field->name + " 的映射值标量宽度认不出来（" + valueSpelling + "）";
                    return false;
                }
            }
            else if (valueCanonical.kind == CXType_Enum)
            {
                field->mapValueKind = 1;
                EnumInfo const* info = findEnumBySpelling(valueSpelling);
                if (info == nullptr)
                {
                    info = findEnumBySpelling(toStd(clang_getTypeSpelling(valueCanonical)));
                }
                if (info == nullptr || !isEnumUnderlyingSpelling(info->underlying))
                {
                    *error = "字段 " + field->name + " 的映射值枚举底层类型认不出来（" + valueSpelling + "）";
                    return false;
                }
                field->mapValueWidthType = info->underlying;
                field->mapValueEnumShort = info->shortName;
            }
            else if (g_valueTypes.count(unqualifiedName(valueSpelling)) > 0)
            {
                field->mapValueKind = 3;
                field->mapValueValueClassShort = unqualifiedName(valueSpelling);
            }
            else if (hasPrefix(valueSpelling, "castl::shared_ptr") ||
                     hasPrefix(valueSpelling, "std::shared_ptr"))
            {
                const std::size_t open = valueSpelling.find('<');
                const std::size_t close = valueSpelling.rfind('>');
                const std::string inner = (open != std::string::npos && close != std::string::npos)
                                              ? bareType(trimCopy(valueSpelling.substr(open + 1, close - open - 1)))
                                              : std::string();
                const std::string innerShort = unqualifiedName(inner);
                if (!g_bindableInterfaceNames.count(innerShort))
                {
                    *error = "字段 " + field->name + " 的映射值是指向未绑定类型的智能指针（" + inner +
                             "）—— design D11 只允许已挂 CA_BIND 的接口（不静默丢字段）";
                    return false;
                }
                field->mapValueKind = 4;
                field->mapValueInterfaceShort = innerShort;
                const std::string mapKnownQualified = knownInterfaceQualified(innerShort);
                field->mapValueInterfaceQualified =
                    mapKnownQualified.empty() ? inner : mapKnownQualified;
            }
            else if (valueCanonical.kind == CXType_Record)
            {
                field->mapValueKind = 2;
                std::string hashInner;
                bool isWrapper = parseHashObjWrapper(valueSpelling, &hashInner);
                field->mapValueIsHashObj = isWrapper;
                // 内层记录声明：HashObj 取值的模板实参 0，普通记录直接取声明。
                CXType recordType = valueCanonical;
                if (isWrapper)
                {
                    recordType = clang_getCanonicalType(clang_Type_getTemplateArgumentAsType(valueCanonical, 0));
                }
                CXCursor declaration = clang_getTypeDeclaration(recordType);
                if (clang_Cursor_isNull(declaration))
                {
                    *error = "字段 " + field->name + " 的映射值记录找不到定义（" + valueSpelling + "）";
                    return false;
                }
                field->mapValueRecordShort = unqualifiedName(toStd(clang_getTypeSpelling(recordType)));
                field->mapValueRecordQualified = qualifiedCursorName(declaration);
                if (!buildSerializedFieldList(declaration, &field->mapValueFields, &field->nestedLayoutText,
                                              "  ", error))
                {
                    return false;
                }
                if (field->mapValueFields.empty())
                {
                    *error = "字段 " + field->name + " 的映射值记录没有字段";
                    return false;
                }
            }
            else
            {
                *error = "字段 " + field->name + " 的映射值形状不在本批支持范围内（" + valueSpelling +
                         "）—— design D11 只允许标量 / 枚举 / 记录 / 值类 / 已绑定接口引用";
                return false;
            }
        }
        // 接口对象引用成员（D11 ③）：`shared_ptr<已挂 CA_BIND 的接口>` → u64 句柄过界，
        // C++ thunk 从绑定对象表反查重建；指向未绑定类型的智能指针生成期点名（不静默丢字段）。
        else if (hasPrefix(bare, "castl::shared_ptr") || hasPrefix(bare, "std::shared_ptr"))
        {
            const std::size_t open = bare.find('<');
            const std::size_t close = bare.rfind('>');
            const std::string inner = (open != std::string::npos && close != std::string::npos)
                                          ? bareType(trimCopy(bare.substr(open + 1, close - open - 1)))
                                          : std::string();
            const std::string innerShort = unqualifiedName(inner);
            if (!g_bindableInterfaceNames.count(innerShort))
            {
                *error = "字段 " + field->name + " 是指向未绑定类型的智能指针（" + inner +
                         "）—— design D11 只允许已挂 CA_BIND 的接口（不静默丢字段）";
                return false;
            }
            field->kind = 6;
            field->interfaceShort = innerShort;
            const std::string interfaceKnownQualified = knownInterfaceQualified(innerShort);
            field->interfaceQualified =
                interfaceKnownQualified.empty() ? inner : interfaceKnownQualified;
        }
        else if (hasPrefix(bare, "castl::set") || hasPrefix(bare, "std::set") ||
                 bare.find("std::array") != std::string::npos ||
                 bare.find("castl::array") != std::string::npos)
        {
            *error = "字段 " + field->name + " 是集合容器 / 定长数组（" + field->cppType +
                     "）—— 本批未实现该形状；恢复路径：真实实例出现时立扩形状批";
            return false;
        }
        else if (bare.find('*') != std::string::npos ||
                 hasPrefix(bare, "castl::shared_ptr") || hasPrefix(bare, "std::shared_ptr") ||
                 hasPrefix(bare, "castl::weak_ptr") || hasPrefix(bare, "std::weak_ptr"))
        {
            *error = "字段 " + field->name + " 是裸指针 / 智能指针（" + field->cppType + "）—— 本批未实现"
                     "该形状；恢复路径：真实实例出现时立扩形状批";
            return false;
        }
        else
        {
            *error = "字段 " + field->name + " 不是标量 / 枚举 / 文本 / 一层容器（" + field->cppType + "）；"
                     "本批只支持这些形状";
            return false;
        }
        // 布局文本由 buildSerializedFieldList 统一追加（父字段行 + 它自己的嵌套行）。
        (void)layoutText;
        return true;
    }

    // 逐字段分类（嵌套记录内联展开）。indent 只影响布局清单的缩进 —— 它也参与块头哈希，
    // 所以缩进规则一旦变了布局哈希就变；规则固定为：顶层无缩进、每下一层加两格。
    bool buildSerializedFieldList(CXCursor recordCursor, std::vector<SerializedField>* fields,
                                  std::string* layoutText, std::string const& indent, std::string* error)
    {
        struct ListSink
        {
            CXCursor record{};
            std::vector<SerializedField>* fields = nullptr;
            std::string* layoutText = nullptr;
            std::string indent;
            std::string* error = nullptr;
            bool ok = true;
        } sink{ recordCursor, fields, layoutText, indent, error, true };
        clang_visitChildren(
            recordCursor,
            [](CXCursor member, CXCursor, CXClientData data) {
                auto* sink = static_cast<ListSink*>(data);
                if (clang_getCursorKind(member) != CXCursor_FieldDecl)
                {
                    return CXChildVisit_Continue;
                }
                SerializedField field;
                if (!buildSerializedField(sink->record, member, &field, sink->layoutText, sink->error))
                {
                    sink->ok = false;
                    return CXChildVisit_Break;
                }
                *sink->layoutText += sink->indent + field.name + ":" + field.cppType + ":" +
                                     std::to_string(field.offsetBytes) + ";" + field.nestedLayoutText;
                sink->fields->push_back(std::move(field));
                return CXChildVisit_Continue;
            },
            &sink);
        return sink.ok;
    }

    bool buildSerializedElementFields(CXType elementType, SerializedField* field,
                                      std::string* layoutText, std::string* error)
    {
        CXCursor declaration = clang_getTypeDeclaration(elementType);
        if (clang_Cursor_isNull(declaration) || clang_isCursorDefinition(declaration) == 0)
        {
            *error = "容器元素记录找不到定义";
            return false;
        }
        field->elementRecordQualified = qualifiedCursorName(declaration);
        // 元素记录与顶层记录走同一份字段分类（D8 扩展：元素记录里也允许文本 / 枚举 / 标量）。
        if (!buildSerializedFieldList(declaration, &field->elementFields, layoutText, "  ", error))
        {
            return false;
        }
        if (field->elementFields.empty())
        {
            *error = "容器元素记录没有字段";
            return false;
        }
        return true;
    }

    bool recordNeedsSerialization(CXCursor record)
    {
        bool needs = false;
        clang_visitChildren(
            record,
            [](CXCursor member, CXCursor, CXClientData data) {
                if (clang_getCursorKind(member) != CXCursor_FieldDecl)
                {
                    return CXChildVisit_Continue;
                }
                const CXType canonical = clang_getCanonicalType(clang_getCursorType(member));
                const std::string spelling = toStd(clang_getTypeSpelling(canonical));
                const std::string bare = bareType(spelling);
                const bool nonScalar =
                    isContainerType(canonical) ||
                    spelling.find("NameHash") != std::string::npos ||
                    spelling.find("PathHash") != std::string::npos;
                if (nonScalar)
                {
                    *static_cast<bool*>(data) = true;
                    return CXChildVisit_Break;
                }
                return CXChildVisit_Continue;
            },
            &needs);
        return needs;
    }

    bool buildSerializedTypeInfo(CXCursor record, std::string const& classBody,
                                 SerializedTypeInfo* info, std::string* error)
    {
        info->shortName = toStd(clang_getCursorSpelling(record));
        info->qualifiedName = qualifiedCursorName(record);
        bool hasVirtual = false;
        clang_visitChildren(
            record,
            [](CXCursor member, CXCursor, CXClientData data) {
                const CXCursorKind kind = clang_getCursorKind(member);
                if (kind == CXCursor_CXXMethod &&
                    (clang_CXXMethod_isVirtual(member) != 0u || clang_CXXMethod_isPureVirtual(member) != 0u))
                {
                    *static_cast<bool*>(data) = true;
                    return CXChildVisit_Break;
                }
                return CXChildVisit_Continue;
            },
            &hasVirtual);
        if (hasVirtual)
        {
            *error = "记录 " + info->shortName + " 含虚函数 —— 只能走镜像快速路径（本批不支持）";
            return false;
        }
        const std::string friendNeedle = "CA_CODEGEN_FRIEND(" + info->shortName + ")";
        bool hasPrivateField = false;
        clang_visitChildren(
            record,
            [](CXCursor member, CXCursor, CXClientData data) {
                if (clang_getCursorKind(member) == CXCursor_FieldDecl &&
                    clang_getCXXAccessSpecifier(member) != CX_CXXPublic)
                {
                    *static_cast<bool*>(data) = true;
                    return CXChildVisit_Break;
                }
                return CXChildVisit_Continue;
            },
            &hasPrivateField);
        if (hasPrivateField && classBody.find(friendNeedle) == std::string::npos)
        {
            *error = "记录 " + info->shortName + " 含私有字段但没有 " + friendNeedle +
                     "（序列化代码要按字段名读写私有成员）";
            return false;
        }
        struct FieldBuildSink
        {
            CXCursor record;
            SerializedTypeInfo* info = nullptr;
            std::string* layoutText = nullptr;
            std::string* error = nullptr;
            bool ok = true;
        } sink{ record, info, &info->layoutText, error, true };
        // 顶层字段与嵌套记录走同一份分类器（D8 扩展：嵌套记录逐字段内联，布局文本一起算哈希）。
        if (!buildSerializedFieldList(sink.record, &info->fields, sink.layoutText, "", error))
        {
            return false;
        }
        if (info->fields.empty())
        {
            *error = "记录 " + info->shortName + " 没有字段";
            return false;
        }
        // 默认构造门放在**形状检查之后**：嵌入记录 / 映射容器 / 两层容器 / 出参方向这些
        // 形状类诊断必须保持既有的点名文本（既有拒绝夹具依赖它们）；没有显式构造函数的类
        // 走隐式默认构造，也是合法的。
        {
            CtorProbe probe;
            clang_visitChildren(record, collectCtorProbe, &probe);
            bool anyCtor = false;
            clang_visitChildren(
                record,
                [](CXCursor member, CXCursor, CXClientData data) {
                    if (clang_getCursorKind(member) == CXCursor_Constructor)
                    {
                        *static_cast<bool*>(data) = true;
                        return CXChildVisit_Break;
                    }
                    return CXChildVisit_Continue;
                },
                &anyCtor);
            if (anyCtor && !probe.hasPublicDefaultCtor)
            {
                *error = "记录 " + info->shortName + " 没有公开默认构造 —— 反序列化要构造栈对象";
                return false;
            }
        }
        info->typeHash = fnv1a64(info->qualifiedName);
        info->layoutHash = fnv1a64(info->layoutText);
        return true;
    }

    std::string escapeCppString(std::string const& text)
    {
        std::string out;
        for (char character : text)
        {
            if (character == '\\' || character == '"')
            {
                out.push_back('\\');
            }
            out.push_back(character);
        }
        return out;
    }

    std::string enumCppNameForShort(std::string const& shortName)
    {
        for (auto const& entry : g_enumsByShort)
        {
            if (entry.second.shortName == shortName)
            {
                return entry.second.cppName;
            }
        }
        return shortName;
    }

    void appendCppWriteField(std::string* code, SerializedField const& field, std::string const& object,
                             std::string const& indent)
    {
        const std::string access = object + "." + field.name;
        if (field.kind == 0)
        {
            *code += indent + "WriteValue<" + field.widthType + ">(out, static_cast<" + field.widthType +
                     ">(" + access + "));\n";
        }
        else if (field.kind == 1)
        {
            *code += indent + "WriteValue<" + field.widthType + ">(out, static_cast<" + field.widthType +
                     ">(" + access + "));\n";
        }
        else if (field.kind == 2)
        {
            const std::string textExpr = (field.textIsNameHash || field.textIsPathHash)
                                             ? (access + ".string()")
                                             : access;
            *code += indent + "{\n";
            *code += indent + "    auto const& textValue = " + textExpr + ";\n";
            *code += indent + "    WriteValue<uint64_t>(out, static_cast<uint64_t>(textValue.size()));\n";
            *code += indent +
                     "    WriteRaw(out, textValue.data(), static_cast<uint64_t>(textValue.size()));\n";
            *code += indent + "}\n";
        }
        else if (field.kind == 4)
        {
            // 嵌套记录（D8 扩展）：逐字段内联写，父字段名不单独占一段。
            for (SerializedField const& sub : field.nestedFields)
            {
                appendCppWriteField(code, sub, access, indent);
            }
        }
        else if (field.kind == 7)
        {
            // HashObj 包装（D11 附）：线形 = 内层记录的字段；写方向访问 `.Get()`。
            for (SerializedField const& sub : field.nestedFields)
            {
                appendCppWriteField(code, sub, access + ".Get()", indent);
            }
        }
        else if (field.kind == 8)
        {
            // 定长数组是输入方向：写路径不产出（元素记录只有结构体、没 Codec；返回位置已生成期拒绝）。
            *code += indent + "/* 定长数组成员：输入方向，不产写路径 */\n";
        }
        else if (field.kind == 9 || field.kind == 10)
        {
            // 值类字段 / 联合体是输入方向：写路径不产出。
            *code += indent + "/* 值类字段 / 联合体：输入方向，不产写路径 */\n";
        }
        else if (field.kind == 5)
        {
            // 命名映射（D11 ②）：count + 重复（key 文本线形, value 线形）。
            *code += indent + "{\n";
            *code += indent + "    WriteValue<uint64_t>(out, static_cast<uint64_t>(" + access +
                     ".size()));\n";
            *code += indent + "    for (auto const& kv : " + access + ")\n" + indent + "    {\n";
            *code += indent + "        {\n";
            *code += indent + "            auto const& keyText = kv.first.string();\n";
            *code += indent + "            WriteValue<uint64_t>(out, static_cast<uint64_t>(keyText.size()));\n";
            *code += indent + "            WriteRaw(out, keyText.data(), "
                              "static_cast<uint64_t>(keyText.size()));\n";
            *code += indent + "        }\n";
            if (field.mapValueKind == 0 || field.mapValueKind == 1)
            {
                *code += indent + "        WriteValue<" + field.mapValueWidthType +
                         ">(out, static_cast<" + field.mapValueWidthType + ">(kv.second));\n";
            }
            else if (field.mapValueKind == 3)
            {
                *code += indent + "        /* 值类是输入方向：写路径不产出（返回位置已生成期拒绝） */\n";
            }
            else if (field.mapValueKind == 4)
            {
                *code += indent + "        /* 接口引用是输入方向：写路径不产出（返回位置已生成期拒绝） */\n";
            }
            else
            {
                std::string valueAccess = "kv.second";
                if (field.mapValueIsHashObj)
                {
                    valueAccess += ".Get()";
                }
                for (SerializedField const& sub : field.mapValueFields)
                {
                    appendCppWriteField(code, sub, valueAccess, indent + "        ");
                }
            }
            *code += indent + "    }\n" + indent + "}\n";
        }
        else if (field.kind == 6)
        {
            // 接口对象引用（D11 ③）：输入方向记录不产写路径；返回位置含此字段在生成期已拒绝。
            *code += indent + "/* 接口对象引用字段 " + field.name +
                     "：输入方向，不产写路径（返回位置已生成期拒绝） */\n";
        }
        else
        {
            *code += indent + "WriteValue<uint64_t>(out, static_cast<uint64_t>(" + access + ".size()));\n";
            *code += indent + "for (auto const& element : " + access + ")\n" + indent + "{\n";
            if (field.elementKind == 0)
            {
                *code += indent + "    WriteValue<" + field.elementWidthType + ">(out, element);\n";
            }
            else if (field.elementKind == 1)
            {
                *code += indent + "    WriteValue<" + field.elementWidthType +
                         ">(out, static_cast<" + field.elementWidthType + ">(element));\n";
            }
            else if (field.elementKind == 3)
            {
                // 文本元素（D8 扩展）：每条 u64 长度 + UTF-8 字节。
                const std::string textExpr = (field.elementTextIsNameHash || field.elementTextIsPathHash)
                                                 ? "element.string()"
                                                 : "element";
                *code += indent + "    {\n";
                *code += indent + "        auto const& textValue = " + textExpr + ";\n";
                *code += indent +
                         "        WriteValue<uint64_t>(out, static_cast<uint64_t>(textValue.size()));\n";
                *code += indent + "        WriteRaw(out, textValue.data(), "
                                  "static_cast<uint64_t>(textValue.size()));\n";
                *code += indent + "    }\n";
            }
            else if (field.elementKind == 4)
            {
                // 值类元素（D11 ①）：输入方向记录不产写路径；返回位置含此元素在生成期已拒绝。
                *code += indent + "    /* 值类元素：输入方向，不产写路径 */\n";
            }
            else
            {
                // 元素记录：写走它自己的 codec（友元只对直接类有效，父 codec 写不了它的私有成员）。
                *code += indent + "        cacore::codegen::CodeGenBindingCodec<" +
                         field.elementRecordQualified + ">::WriteFields(out, element);\n";
            }
            *code += indent + "}\n";
        }
    }

    // D11 ① / ②：把「kind + handle + name」三段值类线形重建成值类对象。
    // 值类容器元素与映射值共用；对象通道走 reader.resolver（绑定对象表反查），查无句柄 ⇒ failureCode。
    void appendCppValueClassReconstruct(std::string* code, std::string const& valueClassShort,
                                        std::string const& target, std::string const& kindVar,
                                        std::string const& handleVar, std::string const& nameVar,
                                        std::string const& fieldPath, std::string const& indent,
                                        unsigned failureCode)
    {
        auto found = g_valueTypes.find(valueClassShort);
        if (found == g_valueTypes.end())
        {
            *code += indent + "/* 值类 " + valueClassShort + " 未登记（不该发生） */\n";
            return;
        }
        const ValueTypeInfo& info = found->second;
        bool firstBranch = true;
        for (ValueTypeInfo::ObjectChannel const& channel : info.objectChannels)
        {
            *code += indent + (firstBranch ? "if (" : "else if (") + kindVar + " == " +
                     std::to_string(channel.kind) + "u)\n" + indent + "{\n";
            *code += indent + "    std::shared_ptr<void> valueObject = reader.resolver.resolve("
                              "reader.resolver.user, " + handleVar + ", \"" +
                     channel.pointeeQualified + "\", \"" + fieldPath + "\");\n";
            *code += indent + "    if (!valueObject) { return " + std::to_string(failureCode) + "u; }\n";
            *code += indent + "    " + target + " = " + info.qualifiedName +
                     "(std::static_pointer_cast<" + channel.pointeeQualified + ">(valueObject));\n";
            *code += indent + "}\n";
            firstBranch = false;
        }
        if (info.hasNameCtor)
        {
            *code += indent + (firstBranch ? "if (" : "else if (") + kindVar + " == " +
                     std::to_string(info.nameKind) + "u)\n" + indent + "{\n";
            *code += indent + "    " + target + " = " + info.qualifiedName +
                     "(cacore::NameHash(" + nameVar + "));\n";
            *code += indent + "}\n";
            firstBranch = false;
        }
        if (info.hasInvalidKind)
        {
            // 无效态 = 原生默认构造值（`DrawCall::New()` 的 `IndexBufferData.indexBufferHandle`
            // 就是这个状态）。记录字段的"未设置"必须能过界，否则每个 AddPass 都会被句柄解析拒。
            *code += indent + (firstBranch ? "if (" : "else if (") + kindVar + " == " +
                     std::to_string(info.invalidKind) + "u)\n" + indent + "{\n";
            *code += indent + "    " + target + " = " + info.qualifiedName + "{};\n";
            *code += indent + "}\n";
            firstBranch = false;
        }
        *code += indent + "else { return " + std::to_string(failureCode) + "u; }\n";
    }

    void appendCppReadField(std::string* code, SerializedField const& field, std::string const& object,
                            std::string const& indent)
    {
        const std::string access = object + "." + field.name;
        if (field.kind == 0 || field.kind == 1)
        {
            *code += indent + "{\n";
            *code += indent + "    " + field.widthType + " value{};\n";
            *code += indent + "    if (!reader.ReadValue(value)) { return 4u; }\n";
            if (field.kind == 1)
            {
                *code += indent + "    " + access + " = static_cast<" + enumCppNameForShort(
                             field.enumShort) + ">(value);\n";
            }
            else
            {
                *code += indent + "    " + access + " = value;\n";
            }
            *code += indent + "}\n";
        }
        else if (field.kind == 2)
        {
            *code += indent + "{\n";
            *code += indent + "    uint64_t textSize = 0u;\n";
            *code += indent +
                     "    if (!reader.ReadValue(textSize) || textSize > reader.Remaining()) { return 4u; }\n";
            *code += indent +
                     "    castl::string text(reinterpret_cast<char const*>(reader.data + reader.pos), "
                     "static_cast<std::size_t>(textSize));\n";
            *code += indent + "    reader.pos += textSize;\n";
            if (field.textIsNameHash)
            {
                *code += indent + "    " + access + " = cacore::NameHash(text);\n";
            }
            else if (field.textIsPathHash)
            {
                *code += indent + "    " + access + " = cacore::PathHash(text);\n";
            }
            else
            {
                *code += indent + "    " + access + " = text;\n";
            }
            *code += indent + "}\n";
        }
        else if (field.kind == 4)
        {
            // 嵌套记录（D8 扩展）：逐字段内联读。
            for (SerializedField const& sub : field.nestedFields)
            {
                appendCppReadField(code, sub, access, indent);
            }
        }
        else if (field.kind == 7)
        {
            // HashObj 包装（D11 附）：读内层字段到临时值，再重新包成 HashObj。
            // 临时变量名必须是**标识符**（`field.name + "Value"`），不能拼成表达式路径 ——
            // 拼 `access + "Value"` 会生成 `T obj.m_ViewPortValue{};`（实测 C3878）。
            const std::string wrapperLocal = field.name + "Value";
            *code += indent + "{\n";
            *code += indent + "    " + field.nestedRecordQualified + " " + wrapperLocal + "{};\n";
            for (SerializedField const& sub : field.nestedFields)
            {
                appendCppReadField(code, sub, wrapperLocal, indent + "    ");
            }
            *code += indent + "    " + access + " = cacore::HashObj<" + field.nestedRecordQualified +
                     ">{" + wrapperLocal + "};\n";
            *code += indent + "}\n";
        }
        else if (field.kind == 8)
        {
            // 定长数组：元素个数固定，线形里**不带 count**；元素按记录字段逐项。
            *code += indent + "{\n";
            *code += indent + "    for (std::size_t index = 0u; index < " +
                     std::to_string(field.fixedCount) + "u; ++index)\n" + indent + "    {\n";
            const std::string elementAccess = access + "[index]";
            for (SerializedField const& sub : field.elementFields)
            {
                appendCppReadField(code, sub, elementAccess, indent + "        ");
            }
            *code += indent + "    }\n" + indent + "}\n";
        }
        else if (field.kind == 9)
        {
            // 值类字段：kind + handle + 名称三段线形 → 值类对象。
            *code += indent + "{\n";
            *code += indent + "    uint32_t valueKind = 0u;\n";
            *code += indent + "    uint64_t valueHandle = 0u;\n";
            *code += indent + "    uint64_t valueNameSize = 0u;\n";
            *code += indent + "    if (!reader.ReadValue(valueKind) || !reader.ReadValue(valueHandle) || "
                              "!reader.ReadValue(valueNameSize) || valueNameSize > reader.Remaining()) "
                              "{ return 4u; }\n";
            *code += indent + "    castl::string valueName(reinterpret_cast<char const*>(reader.data + "
                              "reader.pos), static_cast<std::size_t>(valueNameSize));\n";
            *code += indent + "    reader.pos += valueNameSize;\n";
            appendCppValueClassReconstruct(code, field.valueClassShort, access, "valueKind", "valueHandle",
                                           "valueName", "字段 " + field.name, indent + "    ", 12u);
            *code += indent + "}\n";
        }
        else if (field.kind == 10)
        {
            // 联合体：定长原样字节，不做逐字段解释。
            *code += indent + "if (!reader.ReadRaw(&" + access + ", " + std::to_string(field.blobSize) +
                     "u)) { return 4u; }\n";
        }
        else if (field.kind == 5)
        {
            // 命名映射（D11 ②）：count + 重复（key 文本线形, value 线形）。
            *code += indent + "{\n";
            *code += indent + "    uint64_t count = 0u;\n";
            *code += indent +
                     "    if (!reader.ReadValue(count) || count > reader.Remaining()) { return 4u; }\n";
            *code += indent + "    " + access + ".clear();\n";
            *code += indent + "    for (uint64_t index = 0u; index < count; ++index)\n" + indent + "    {\n";
            *code += indent + "        uint64_t keySize = 0u;\n";
            *code += indent +
                     "        if (!reader.ReadValue(keySize) || keySize > reader.Remaining()) { return 4u; }\n";
            *code += indent + "        castl::string keyText(reinterpret_cast<char const*>(reader.data + "
                              "reader.pos), static_cast<std::size_t>(keySize));\n";
            *code += indent + "        reader.pos += keySize;\n";
            *code += indent + "        cacore::NameHash key(keyText);\n";
            if (field.mapValueKind == 0 || field.mapValueKind == 1)
            {
                *code += indent + "        " + field.mapValueWidthType + " mapValue{};\n";
                *code += indent + "        if (!reader.ReadValue(mapValue)) { return 4u; }\n";
                if (field.mapValueKind == 1)
                {
                    *code += indent + "        " + access + "[key] = static_cast<" +
                             enumCppNameForShort(field.mapValueEnumShort) + ">(mapValue);\n";
                }
                else
                {
                    *code += indent + "        " + access + "[key] = mapValue;\n";
                }
            }
            else if (field.mapValueKind == 3)
            {
                *code += indent + "        uint32_t valueKind = 0u;\n";
                *code += indent + "        uint64_t valueHandle = 0u;\n";
                *code += indent + "        uint64_t valueNameSize = 0u;\n";
                *code += indent + "        if (!reader.ReadValue(valueKind) || "
                                  "!reader.ReadValue(valueHandle) || !reader.ReadValue(valueNameSize) || "
                                  "valueNameSize > reader.Remaining()) { return 4u; }\n";
                *code += indent + "        castl::string valueName(reinterpret_cast<char const*>(reader.data + "
                                  "reader.pos), static_cast<std::size_t>(valueNameSize));\n";
                *code += indent + "        reader.pos += valueNameSize;\n";
                appendCppValueClassReconstruct(code, field.mapValueValueClassShort, access + "[key]",
                                               "valueKind", "valueHandle", "valueName",
                                               "字段 " + field.name, indent + "        ", 12u);
            }
            else if (field.mapValueKind == 4)
            {
                *code += indent + "        uint64_t valueHandle = 0u;\n";
                *code += indent + "        if (!reader.ReadValue(valueHandle)) { return 4u; }\n";
                *code += indent +
                         "        std::shared_ptr<void> valueObject = reader.resolver.resolve("
                         "reader.resolver.user, valueHandle, \"" + field.mapValueInterfaceQualified +
                         "\", \"" + field.name + "\");\n";
                *code += indent + "        if (!valueObject) { return 12u; }\n";
                *code += indent + "        " + access + "[key] = std::static_pointer_cast<" +
                         field.mapValueInterfaceQualified + ">(valueObject);\n";
            }
            else
            {
                *code += indent + "        " + field.mapValueRecordQualified + " mapValue{};\n";
                std::string valueAccess = "mapValue";
                if (field.mapValueIsHashObj)
                {
                    // HashObj 值：先读到内层记录，再包成 HashObj 存进表。
                    for (SerializedField const& sub : field.mapValueFields)
                    {
                        appendCppReadField(code, sub, valueAccess, indent + "        ");
                    }
                    *code += indent + "        " + access + "[key] = cacore::HashObj<" +
                             field.mapValueRecordQualified + ">{mapValue};\n";
                }
                else
                {
                    for (SerializedField const& sub : field.mapValueFields)
                    {
                        appendCppReadField(code, sub, valueAccess, indent + "        ");
                    }
                    *code += indent + "        " + access + "[key] = mapValue;\n";
                }
            }
            *code += indent + "    }\n" + indent + "}\n";
        }
        else if (field.kind == 6)
        {
            // 接口对象引用（D11 ③）：u64 句柄 → 从绑定对象表反查重建；查无句柄 ⇒ 12（边界拒）。
            *code += indent + "{\n";
            *code += indent + "    uint64_t objectHandle = 0u;\n";
            *code += indent + "    if (!reader.ReadValue(objectHandle)) { return 4u; }\n";
            *code += indent + "    if (objectHandle == 0u) { " + access + " = nullptr; }\n";
            *code += indent + "    else\n" + indent + "    {\n";
            *code += indent + "        std::shared_ptr<void> object = reader.resolver.resolve("
                              "reader.resolver.user, objectHandle, \"" + field.interfaceQualified +
                     "\", \"" + field.name + "\");\n";
            *code += indent + "        if (!object) { return 12u; }\n";
            *code += indent + "        " + access + " = std::static_pointer_cast<" +
                     field.interfaceQualified + ">(object);\n";
            *code += indent + "    }\n" + indent + "}\n";
        }
        else
        {
            *code += indent + "{\n";
            *code += indent + "    uint64_t count = 0u;\n";
            *code += indent +
                     "    if (!reader.ReadValue(count) || count > reader.Remaining()) { return 4u; }\n";
            *code += indent + "    " + access + ".clear();\n";
            *code += indent + "    for (uint64_t index = 0u; index < count; ++index)\n" + indent + "    {\n";
            if (field.elementKind == 0)
            {
                *code += indent + "        " + field.elementWidthType + " element{};\n";
                *code += indent + "        if (!reader.ReadValue(element)) { return 4u; }\n";
                *code += indent + "        " + access + ".push_back(element);\n";
            }
            else if (field.elementKind == 1)
            {
                *code += indent + "        " + field.elementWidthType + " element{};\n";
                *code += indent + "        if (!reader.ReadValue(element)) { return 4u; }\n";
                *code += indent + "        " + access + ".push_back(static_cast<" +
                         enumCppNameForShort(field.elementEnumShort) + ">(element));\n";
            }
            else if (field.elementKind == 3)
            {
                // 文本元素（D8 扩展）：读长度 + UTF-8 字节后按元素类型重建。
                *code += indent + "        uint64_t textSize = 0u;\n";
                *code += indent +
                         "        if (!reader.ReadValue(textSize) || textSize > reader.Remaining()) "
                         "{ return 4u; }\n";
                *code += indent +
                         "        castl::string text(reinterpret_cast<char const*>(reader.data + reader.pos), "
                         "static_cast<std::size_t>(textSize));\n";
                *code += indent + "        reader.pos += textSize;\n";
                if (field.elementTextIsNameHash)
                {
                    *code += indent + "        " + access + ".push_back(cacore::NameHash(text));\n";
                }
                else if (field.elementTextIsPathHash)
                {
                    *code += indent + "        " + access + ".push_back(cacore::PathHash(text));\n";
                }
                else
                {
                    *code += indent + "        " + access + ".push_back(text);\n";
                }
            }
            else if (field.elementKind == 4)
            {
                // 值类元素（D11 ①）：kind + handle + name 三段线形 → 值类对象。
                *code += indent + "        uint32_t valueKind = 0u;\n";
                *code += indent + "        uint64_t valueHandle = 0u;\n";
                *code += indent + "        uint64_t valueNameSize = 0u;\n";
                *code += indent + "        if (!reader.ReadValue(valueKind) || "
                                  "!reader.ReadValue(valueHandle) || !reader.ReadValue(valueNameSize) || "
                                  "valueNameSize > reader.Remaining()) { return 4u; }\n";
                *code += indent + "        castl::string valueName(reinterpret_cast<char const*>(reader.data + "
                                  "reader.pos), static_cast<std::size_t>(valueNameSize));\n";
                *code += indent + "        reader.pos += valueNameSize;\n";
                auto valueInfo = g_valueTypes.find(field.elementValueClassShort);
                const std::string valueType =
                    valueInfo == g_valueTypes.end() ? field.elementValueClassShort
                                                    : valueInfo->second.qualifiedName;
                *code += indent + "        " + valueType + " element{};\n";
                appendCppValueClassReconstruct(code, field.elementValueClassShort, "element", "valueKind",
                                               "valueHandle", "valueName", "字段 " + field.name,
                                               indent + "        ", 12u);
                *code += indent + "        " + access + ".push_back(element);\n";
            }
            else if (field.elementKind == 2)
            {
                // 元素记录由它自己的 codec 读写（友元只对直接类有效，父 codec 读不到私有成员）。
                *code += indent + "        " + field.elementRecordQualified + " element{};\n";
                *code += indent + "        {\n";
                *code += indent + "            const uint32_t elementCode = "
                                  "cacore::codegen::CodeGenBindingCodec<" +
                         field.elementRecordQualified + ">::ReadFields(reader, element);\n";
                *code += indent + "            if (elementCode != 0u) { return elementCode; }\n";
                *code += indent + "        }\n";
                *code += indent + "        " + access + ".push_back(element);\n";
            }
            *code += indent + "    }\n" + indent + "}\n";
        }
    }

    // 生成 C++ 编解码：WriteValue / WriteRaw / Serialize / Reader / Deserialize。
    // 内层元素记录要由**它自己的** codec 来读写：`CA_CODEGEN_FRIEND(T)` 只让
    // `CodeGenBindingCodec<T>` 成为 T 的友元，父记录的 codec 不是 —— 父 codec 直接内联读
    // 私有成员会撞 C2248（实测 `DrawCall::m_BoundVertexBuffers` 从 RenderPass 的 codec 里读）。
    // 这里把每个被引用的元素记录单独发一份 codec 特化（只有字段级 ReadFields / WriteFields，
    // 没有块头），父 codec 调它。用宏守卫防止同一个元素记录在多个父 codec 里重复定义。
    void appendElementCodecSpec(std::string* code, SerializedElementInfo const& element)
    {
        const std::string guard = "CODEGEN_ELEMENT_CODEC_" + element.shortName;
        *code += "#ifndef " + guard + "\n#define " + guard + "\n";
        *code += "namespace cacore::codegen\n{\n";
        *code += "template<>\nstruct CodeGenBindingCodec<" + element.qualifiedName + ">\n{\n";
        *code += "    template<typename ReaderT>\n";
        *code += "    static uint32_t ReadFields(ReaderT& reader, " + element.qualifiedName +
                 "& obj)\n    {\n";
        std::string reads;
        for (SerializedField const& field : element.fields)
        {
            appendCppReadField(&reads, field, "obj", "        ");
        }
        *code += reads;
        *code += "        return 0u;\n    }\n";
        *code += "    static void WriteRaw(std::vector<uint8_t>& out, void const* data, uint64_t size)\n"
                 "    {\n";
        *code += "        uint8_t const* bytes = static_cast<uint8_t const*>(data);\n";
        *code += "        out.insert(out.end(), bytes, bytes + size);\n    }\n";
        *code += "    template<typename V> static void WriteValue(std::vector<uint8_t>& out, V value)\n"
                 "    {\n        WriteRaw(out, &value, sizeof(V));\n    }\n";
        *code += "    static void WriteFields(std::vector<uint8_t>& out, " + element.qualifiedName +
                 " const& obj)\n    {\n";
        std::string writes;
        for (SerializedField const& field : element.fields)
        {
            appendCppWriteField(&writes, field, "obj", "        ");
        }
        *code += writes;
        *code += "    }\n};\n}\n";
        *code += "#endif\n";
    }

    // 这条记录用到的元素记录短名（递归收集：容器元素 / 定长数组元素 / 映射的记录值）。
    void collectElementRecords(SerializedField const& field, std::set<std::string>* names)
    {
        if ((field.kind == 3 || field.kind == 8) && field.elementKind == 2 &&
            !field.elementRecordShort.empty())
        {
            names->insert(field.elementRecordShort);
        }
        if (field.kind == 5 && field.mapValueKind == 2 && !field.mapValueRecordShort.empty())
        {
            names->insert(field.mapValueRecordShort);
        }
        for (SerializedField const& sub : field.nestedFields)
        {
            collectElementRecords(sub, names);
        }
        for (SerializedField const& sub : field.elementFields)
        {
            collectElementRecords(sub, names);
        }
        for (SerializedField const& sub : field.mapValueFields)
        {
            collectElementRecords(sub, names);
        }
    }

    std::string emitSerializedCodecCpp(SerializedTypeInfo const& info)
    {
        std::string code;
        // 先发本记录引用到的元素记录 codec（父 codec 要调它们的字段级读写）。
        // **递归且先发依赖**：元素记录自己也可能引用别的元素记录（DrawCallBatch → 映射值
        // VertexInputsDescriptor → vector<VertexAttribute>），被引用的必须先定义。
        {
            std::set<std::string> emittedElements;
            std::function<void(std::string const&)> emitElement = [&](std::string const& name) {
                if (!emittedElements.insert(name).second)
                {
                    return;
                }
                auto element = g_serializedElementRecords.find(name);
                if (element == g_serializedElementRecords.end())
                {
                    return;
                }
                std::set<std::string> dependencies;
                for (SerializedField const& field : element->second.fields)
                {
                    collectElementRecords(field, &dependencies);
                }
                for (std::string const& dependency : dependencies)
                {
                    if (dependency != name)
                    {
                        emitElement(dependency);
                    }
                }
                appendElementCodecSpec(&code, element->second);
            };
            std::set<std::string> elementNames;
            for (SerializedField const& field : info.fields)
            {
                collectElementRecords(field, &elementNames);
            }
            for (std::string const& name : elementNames)
            {
                emitElement(name);
            }
        }
        code += "// 序列化块记录（任务 3.2）：" + info.qualifiedName + "\n";
        code += "namespace cacore::codegen\n{\n";
        code += "template<>\nstruct CodeGenBindingCodec<" + info.qualifiedName + ">\n{\n";
        code += "    static constexpr uint64_t kTypeNameHash = " + std::to_string(info.typeHash) +
                "ull;\n";
        code += "    static constexpr uint64_t kLayoutHash = " + std::to_string(info.layoutHash) +
                "ull;\n";
        code += "    static constexpr char const* kLayoutText = \"" + escapeCppString(info.layoutText) +
                "\";\n";
        code += "    static uint64_t Fnv(char const* text, uint64_t size)\n    {\n";
        code += "        uint64_t hash = 14695981039346656037ull;\n";
        code += "        for (uint64_t i = 0; i < size; ++i) { hash ^= "
                "static_cast<uint8_t>(text[i]); hash *= 1099511628211ull; }\n";
        code += "        return hash;\n    }\n";
        code += "    static void WriteRaw(std::vector<uint8_t>& out, void const* data, uint64_t size)\n"
                "    {\n";
        code += "        uint8_t const* bytes = static_cast<uint8_t const*>(data);\n";
        code += "        out.insert(out.end(), bytes, bytes + size);\n    }\n";
        code += "    template<typename T> static void WriteValue(std::vector<uint8_t>& out, T value)\n"
                "    {\n        WriteRaw(out, &value, sizeof(T));\n    }\n";
        code += "    static void Serialize(" + info.qualifiedName +
                " const& obj, std::vector<uint8_t>& out)\n    {\n";
        code += "        out.clear();\n";
        code += "        WriteValue<uint32_t>(out, 1u);\n";
        code += "        WriteValue<uint64_t>(out, kTypeNameHash);\n";
        code += "        WriteValue<uint64_t>(out, kLayoutHash);\n";
        std::string writes;
        for (SerializedField const& field : info.fields)
        {
            appendCppWriteField(&writes, field, "obj", "        ");
        }
        code += writes;
        code += "    }\n";
        code += "    struct Reader\n    {\n";
        code += "        uint8_t const* data;\n        uint64_t size;\n        uint64_t pos;\n";
        // D11：值类容器元素 / 接口对象引用要按句柄从绑定对象表反查重建 —— 解析器由 thunk 注入
        // （它持有 `self`，能调 `<接口>_lookupStrongHandle`）。没有解析器时遇到这类字段 ⇒ 12。
        code += "        cacore::codegen::BindingHandleResolver resolver;\n";
        code += "        bool ReadRaw(void* dst, uint64_t count)\n        {\n";
        code += "            if (count > size - pos) { return false; }\n";
        code += "            ::memcpy(dst, data + pos, static_cast<size_t>(count));\n";
        code += "            pos += count;\n            return true;\n        }\n";
        code += "        template<typename T> bool ReadValue(T& value) { return ReadRaw(&value, "
                "sizeof(T)); }\n";
        code += "        uint64_t Remaining() const { return size - pos; }\n    };\n";
        // 三参重载（无对象解析器）供不含新类别字段的记录使用，保持既有调用点不变。
        code += "    static uint32_t Deserialize(" + info.qualifiedName +
                "& obj, uint8_t const* data, uint64_t size)\n    {\n";
        code += "        return Deserialize(obj, data, size, cacore::codegen::BindingHandleResolver{});\n    }\n";
        code += "    static uint32_t Deserialize(" + info.qualifiedName +
                "& obj, uint8_t const* data, uint64_t size, cacore::codegen::BindingHandleResolver resolver)\n    {\n";
        code += "        Reader reader{data, size, 0u, resolver};\n";
        code += "        uint32_t version = 0u;\n";
        code += "        if (!reader.ReadValue(version)) { return 4u; }\n";
        code += "        if (version != 1u) { return 1u; }\n";
        code += "        uint64_t typeHash = 0u;\n";
        code += "        if (!reader.ReadValue(typeHash)) { return 4u; }\n";
        code += "        if (typeHash != kTypeNameHash) { return 2u; }\n";
        code += "        uint64_t layoutHash = 0u;\n";
        code += "        if (!reader.ReadValue(layoutHash)) { return 4u; }\n";
        code += "        if (layoutHash != kLayoutHash) { return 3u; }\n";
        std::string reads;
        for (SerializedField const& field : info.fields)
        {
            appendCppReadField(&reads, field, "obj", "        ");
        }
        code += reads;
        code += "        return 0u;\n    }\n};\n}\n\n";
        return code;
    }

    std::string csharpTypeForSerializedField(SerializedField const& field)
    {
        if (field.kind == 0) return csharpScalarTypeForWidth(field.widthType);
        if (field.kind == 1) return field.enumShort;
        if (field.kind == 2) return "string";
        if (field.kind == 4) return field.nestedRecordShort;
        if (field.kind == 7) return field.nestedRecordShort;
        if (field.kind == 6) return field.interfaceShort;
        if (field.kind == 8) return field.elementRecordShort + "[]";
        if (field.kind == 9) return field.valueClassShort;
        if (field.kind == 10) return "byte[]";
        if (field.kind == 5)
        {
            std::string valueType;
            if (field.mapValueKind == 0) valueType = csharpScalarTypeForWidth(field.mapValueWidthType);
            else if (field.mapValueKind == 1) valueType = field.mapValueEnumShort;
            else if (field.mapValueKind == 2) valueType = field.mapValueRecordShort;
            else if (field.mapValueKind == 3) valueType = field.mapValueValueClassShort;
            else valueType = field.mapValueInterfaceShort;
            return "System.Collections.Generic.Dictionary<string, " + valueType + ">";
        }
        if (field.elementKind == 0) return csharpScalarTypeForWidth(field.elementWidthType) + "[]";
        if (field.elementKind == 1) return field.elementEnumShort + "[]";
        if (field.elementKind == 3) return "string[]";
        if (field.elementKind == 4) return field.elementValueClassShort + "[]";
        return field.elementRecordShort + "[]";
    }

    std::string emitSerializedCodecCSharp(SerializedTypeInfo const& info);

    std::string emitSerializedRecordCSharp(SerializedTypeInfo const& info)
    {
        std::string cs;
        cs += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
        cs += "// 共享值类型（序列化块记录）：托管镜像 + 与 C++ 同源字段清单生成的序列化器。\n";
        cs += "// 限定名：" + info.qualifiedName + "\n";
        cs += "using System;\n\n";
        cs += "namespace " + cSharpNamespaceOf(info.qualifiedName) + "\n{\n";
        cs += "    /// <summary>" + info.shortName + " 的托管镜像：字段名与顺序与 C++ 声明一致</summary>\n";
        cs += "    public sealed class " + info.shortName + "\n    {\n";
        for (SerializedField const& field : info.fields)
        {
            cs += "        public " + csharpTypeForSerializedField(field) + " " + field.name + ";\n";
        }
        cs += "    }\n\n";
        cs += emitSerializedCodecCSharp(info);
        cs += "}\n";
        return cs;
    }

    std::string emitSerializedElementCSharp(SerializedElementInfo const& info)
    {
        std::string cs;
        cs += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
        cs += "// 共享值类型（序列化块的容器元素记录）：纯标量字段。\n";
        cs += "// 限定名：" + info.qualifiedName + "\n";
        cs += "using System;\n\n";
        cs += "namespace " + cSharpNamespaceOf(info.qualifiedName) + "\n{\n";
        cs += "    /// <summary>" + info.shortName + " 的托管镜像（记录字段）</summary>\n";
        cs += "    public struct " + info.shortName + "\n    {\n";
        for (SerializedField const& field : info.fields)
        {
            // 元素记录的字段类型走同一张映射表（标量 / 枚举 / 文本 / 记录），不再只认标量：
            // 元数据记录里的 `cacore::NameHash m_Name` 是文本字段，只按标量映射会生成
            // `public  m_Name;`（实测 CS1519）。
            cs += "        public " + csharpTypeForSerializedField(field) + " " + field.name + ";\n";
        }
        cs += "    }\n}\n";
        return cs;
    }

    std::string emitSerializedFieldCSharpWrite(SerializedField const& field, std::string const& object,
                                               std::string const& indent)
    {
        std::string code;
        const std::string access = object + "." + field.name;
        // 局部名一律带字段后缀：嵌套记录三层时 `pair` / `item` / `textBytes` 会在 C# 里撞成
        // 同名局部变量（实测 CS0136）。
        const std::string sfx = "_" + field.name;
        if (field.kind == 0)
        {
            if (field.widthType == "bool")
            {
                code += indent + "writer.Write((" + object + "." + field.name + " ? (byte)1 : (byte)0));\n";
            }
            else
            {
                code += indent + "writer.Write((" + csharpScalarTypeForWidth(field.widthType) + ")" +
                        access + ");\n";
            }
        }
        else if (field.kind == 1)
        {
            code += indent + "writer.Write((" + csharpScalarTypeForWidth(field.widthType) + ")" + access +
                    ");\n";
        }
        else if (field.kind == 2)
        {
            code += indent + "{\n";
            code += indent + "    byte[] textBytes" + sfx + " = System.Text.Encoding.UTF8.GetBytes(" + access +
                    " ?? string.Empty);\n";
            code += indent + "    writer.Write((ulong)textBytes" + sfx + ".Length);\n";
            code += indent + "    writer.Write(textBytes" + sfx + ");\n";
            code += indent + "}\n";
        }
        else if (field.kind == 4)
        {
            // 嵌套记录（D8 扩展）：复用子记录编解码的字段级 Write。
            code += indent + field.nestedRecordShort + "Codec.Write(writer, " + access + ");\n";
        }
        else if (field.kind == 7)
        {
            // HashObj 包装（D11 附）：托管镜像就是内层记录，线形与嵌套记录一致。
            code += indent + field.nestedRecordShort + "Codec.Write(writer, " + access + ");\n";
        }
        else if (field.kind == 5)
        {
            // 命名映射（D11 ②）：count + 重复（key 文本线形, value 线形）。
            code += indent + "{\n";
            code += indent + "    writer.Write((ulong)(" + access + " == null ? 0 : " + access +
                    ".Count));\n";
            code += indent + "    if (" + access + " != null)\n" + indent + "    {\n";
            code += indent + "        foreach (var pair" + sfx + " in " + access + ")\n" + indent + "        {\n";
            code += indent + "            byte[] keyBytes" + sfx + " = System.Text.Encoding.UTF8.GetBytes(pair" +
                    sfx + ".Key ?? string.Empty);\n";
            code += indent + "            writer.Write((ulong)keyBytes" + sfx + ".Length);\n";
            code += indent + "            writer.Write(keyBytes" + sfx + ");\n";
            if (field.mapValueKind == 0 || field.mapValueKind == 1)
            {
                code += indent + "            writer.Write((" +
                        csharpScalarTypeForWidth(field.mapValueWidthType) + ")pair" + sfx + ".Value);\n";
            }
            else if (field.mapValueKind == 3)
            {
                // 值类值：kind + handle + 名称三段线形。
                code += indent + "            writer.Write(pair" + sfx + ".Value.Kind);\n";
                code += indent + "            writer.Write(pair" + sfx + ".Value.Handle);\n";
                code += indent + "            {\n";
                code += indent + "                byte[] valueNameBytes" + sfx +
                        " = System.Text.Encoding.UTF8.GetBytes(pair" + sfx +
                        ".Value.Name ?? string.Empty);\n";
                code += indent + "                writer.Write((ulong)valueNameBytes" + sfx + ".Length);\n";
                code += indent + "                writer.Write(valueNameBytes" + sfx + ");\n";
                code += indent + "            }\n";
            }
            else if (field.mapValueKind == 4)
            {
                code += indent + "            writer.Write((ulong)(pair" + sfx +
                        ".Value == null ? 0UL : (ulong)pair" + sfx + ".Value.Token));\n";
            }
            else
            {
                // 记录值：逐字段内联写（元素记录只有结构体、没有 Codec，不能直接复用 Codec）。
                for (SerializedField const& sub : field.mapValueFields)
                {
                    code += emitSerializedFieldCSharpWrite(sub, "pair" + sfx + ".Value",
                                                          indent + "            ");
                }
            }
            code += indent + "        }\n" + indent + "    }\n" + indent + "}\n";
        }
        else if (field.kind == 6)
        {
            // 接口对象引用（D11 ③）：按绑定对象句柄过界（0 = 空）。
            code += indent + "writer.Write((ulong)(" + access + " == null ? 0UL : (ulong)" + access +
                    ".Token));\n";
        }
        else if (field.kind == 8)
        {
            // 定长数组成员：元素个数由类型给定，线形里**不写 count**（只写 N 个元素）。
            code += indent + "if (" + access + " != null)\n" + indent + "{\n";
            code += indent + "    foreach (var item" + sfx + " in " + access + ")\n" + indent + "    {\n";
            for (SerializedField const& sub : field.elementFields)
            {
                code += emitSerializedFieldCSharpWrite(sub, "item" + sfx, indent + "        ");
            }
            code += indent + "    }\n" + indent + "}\n";
        }
        else if (field.kind == 9)
        {
            // 值类字段：kind + 句柄 + 名称三段线形（与容器元素/映射值同一份托管值类）。
            code += indent + "{\n";
            code += indent + "    writer.Write(" + access + ".Kind);\n";
            code += indent + "    writer.Write(" + access + ".Handle);\n";
            code += indent + "    byte[] valueNameBytes" + sfx + " = System.Text.Encoding.UTF8.GetBytes(" +
                    access + ".Name ?? string.Empty);\n";
            code += indent + "    writer.Write((ulong)valueNameBytes" + sfx + ".Length);\n";
            code += indent + "    writer.Write(valueNameBytes" + sfx + ");\n";
            code += indent + "}\n";
        }
        else if (field.kind == 10)
        {
            // 联合体：定长原样字节；长度不齐就当场抛（不静默截断/补零）。
            code += indent + "{\n";
            code += indent + "    byte[] raw" + sfx + " = " + access + " ?? new byte[" +
                    std::to_string(field.blobSize) + "];\n";
            code += indent + "    if (raw" + sfx + ".Length != " + std::to_string(field.blobSize) + ")\n" +
                    indent + "    {\n";
            code += indent + "        throw new System.ArgumentException(\"记录字段 " + field.name +
                    " 要求 " + std::to_string(field.blobSize) + " 字节（联合体原样过界）\");\n";
            code += indent + "    }\n";
            code += indent + "    writer.Write(raw" + sfx + ");\n";
            code += indent + "}\n";
        }
        else
        {
            code += indent + "writer.Write((ulong)(" + access + " == null ? 0 : " + access +
                    ".Length));\n";
            code += indent + "if (" + access + " != null)\n" + indent + "{\n";
            code += indent + "    foreach (var item" + sfx + " in " + access + ")\n" + indent + "    {\n";
            if (field.elementKind == 0)
            {
                code += indent + "        writer.Write(item" + sfx + ");\n";
            }
            else if (field.elementKind == 1)
            {
                code += indent + "        writer.Write((" +
                        csharpScalarTypeForWidth(field.elementWidthType) + ")item" + sfx + ");\n";
            }
            else if (field.elementKind == 3)
            {
                // 文本元素（D8 扩展）：每条 u64 长度 + UTF-8 字节。
                code += indent + "        {\n";
                code += indent + "            byte[] textBytes" + sfx +
                        " = System.Text.Encoding.UTF8.GetBytes(item" + sfx + " ?? string.Empty);\n";
                code += indent + "            writer.Write((ulong)textBytes" + sfx + ".Length);\n";
                code += indent + "            writer.Write(textBytes" + sfx + ");\n";
                code += indent + "        }\n";
            }
            else if (field.elementKind == 4)
            {
                // 值类元素（D11 ①）：kind + handle + 名称三段线形。
                code += indent + "        {\n";
                code += indent + "            writer.Write(item" + sfx + ".Kind);\n";
                code += indent + "            writer.Write(item" + sfx + ".Handle);\n";
                code += indent + "            byte[] elementNameBytes" + sfx +
                        " = System.Text.Encoding.UTF8.GetBytes(item" + sfx + ".Name ?? string.Empty);\n";
                code += indent + "            writer.Write((ulong)elementNameBytes" + sfx + ".Length);\n";
                code += indent + "            writer.Write(elementNameBytes" + sfx + ");\n";
                code += indent + "        }\n";
            }
            else
            {
                for (SerializedField const& sub : field.elementFields)
                {
                    code += emitSerializedFieldCSharpWrite(sub, "item" + sfx, indent + "        ");
                }
            }
            code += indent + "    }\n" + indent + "}\n";
        }
        return code;
    }

    // 字段级读（D8 扩展：元数据记录两段式投影的托管侧解包）。与 C++ `Deserialize` 的字段格式逐字节同源。
    std::string emitSerializedFieldCSharpRead(SerializedField const& field, std::string const& object,
                                              std::string const& indent)
    {
        std::string code;
        const std::string access = object + "." + field.name;
        if (field.kind == 0)
        {
            if (field.widthType == "bool")
            {
                code += indent + access + " = reader.ReadByte() != 0;\n";
            }
            else
            {
                code += indent + access + " = " + csharpReaderCall(field.widthType) + ";\n";
            }
        }
        else if (field.kind == 1)
        {
            code += indent + access + " = (" + field.enumShort + ")" + csharpReaderCall(field.widthType) +
                    ";\n";
        }
        else if (field.kind == 2)
        {
            code += indent + "{\n";
            code += indent + "    ulong textSize = reader.ReadUInt64();\n";
            code += indent + "    byte[] textBytes = reader.ReadBytes((int)textSize);\n";
            code += indent + "    " + access +
                    " = System.Text.Encoding.UTF8.GetString(textBytes, 0, textBytes.Length);\n";
            code += indent + "}\n";
        }
        else if (field.kind == 4)
        {
            code += indent + access + " = " + field.nestedRecordShort + "Codec.Read(reader);\n";
        }
        else if (field.kind == 7)
        {
            // HashObj 包装（D11 附）：托管镜像就是内层记录。
            code += indent + access + " = " + field.nestedRecordShort + "Codec.Read(reader);\n";
        }
        else if (field.kind == 8 || field.kind == 9 || field.kind == 10)
        {
            // 定长数组 / 值类字段 / 联合体：本批只定义输入方向（C# → 原生）。返回位置含这些字段
            // 在生成期已被拒绝（design D11 的 MUST NOT），走到这里只可能是手改产物。
            code += indent + "throw new System.IO.InvalidDataException(\"记录字段 " + field.name +
                    " 是" + (field.kind == 8 ? "定长数组" : (field.kind == 9 ? "值类字段" : "联合体")) +
                    "：本批只定义输入方向，不支持从字节反序列化\");\n";
        }
        else if (field.kind == 5 || field.kind == 6)
        {
            // 命名映射 / 接口对象引用：本批只定义输入方向（C# → 原生）。返回位置含这两类字段
            // 在生成期已被拒绝（design D11 的 MUST NOT），走到这里只可能是手改产物。
            code += indent + "throw new System.IO.InvalidDataException(\"记录字段 " + field.name +
                    " 是" + (field.kind == 5 ? "命名映射" : "接口对象引用") +
                    "：本批只定义输入方向，不支持从字节反序列化\");\n";
        }
        else
        {
            // 容器元素（标量 / 枚举 / 文本 / 值类 / 记录）：局部名一律带字段后缀 ——
            // 嵌套记录三层时 `count` / `index` / `item` 会在 C# 里撞成同名局部变量（实测 CS0136）。
            const std::string sfx = "_" + field.name;
            std::string elementType;
            if (field.elementKind == 0) elementType = csharpScalarTypeForWidth(field.elementWidthType);
            else if (field.elementKind == 1) elementType = field.elementEnumShort;
            else if (field.elementKind == 3) elementType = "string";
            else if (field.elementKind == 4) elementType = field.elementValueClassShort;
            else elementType = field.elementRecordShort;
            code += indent + "{\n";
            code += indent + "    ulong count" + sfx + " = reader.ReadUInt64();\n";
            code += indent + "    " + access + " = new " + elementType + "[(int)count" + sfx + "];\n";
            code += indent + "    for (int index" + sfx + " = 0; index" + sfx + " < (int)count" + sfx +
                    "; ++index" + sfx + ")\n" + indent + "    {\n";
            const std::string elementRef = access + "[index" + sfx + "]";
            if (field.elementKind == 0)
            {
                code += indent + "        " + elementRef + " = " +
                        csharpReaderCall(field.elementWidthType) + ";\n";
            }
            else if (field.elementKind == 1)
            {
                code += indent + "        " + elementRef + " = (" + field.elementEnumShort + ")" +
                        csharpReaderCall(field.elementWidthType) + ";\n";
            }
            else if (field.elementKind == 3)
            {
                code += indent + "        {\n";
                code += indent + "            ulong textSize" + sfx + " = reader.ReadUInt64();\n";
                code += indent + "            byte[] textBytes" + sfx + " = reader.ReadBytes((int)textSize" +
                        sfx + ");\n";
                code += indent + "            " + elementRef +
                        " = System.Text.Encoding.UTF8.GetString(textBytes" + sfx + ", 0, textBytes" + sfx +
                        ".Length);\n";
                code += indent + "        }\n";
            }
            else if (field.elementKind == 4)
            {
                // 值类元素（D11 ①）：kind + handle + 名称三段线形 → 托管值类（同程序集内部构造器）。
                code += indent + "        {\n";
                code += indent + "            uint valueKind" + sfx + " = reader.ReadUInt32();\n";
                code += indent + "            ulong valueHandle" + sfx + " = reader.ReadUInt64();\n";
                code += indent + "            ulong valueNameSize" + sfx + " = reader.ReadUInt64();\n";
                code += indent + "            byte[] valueNameBytes" + sfx +
                        " = reader.ReadBytes((int)valueNameSize" + sfx + ");\n";
                code += indent + "            string valueName" + sfx +
                        " = System.Text.Encoding.UTF8.GetString(valueNameBytes" + sfx + ", 0, valueNameBytes" +
                        sfx + ".Length);\n";
                code += indent + "            " + elementRef + " = new " + field.elementValueClassShort +
                        "(valueKind" + sfx + ", valueHandle" + sfx + ", valueName" + sfx + ");\n";
                code += indent + "        }\n";
            }
            else
            {
                code += indent + "        " + field.elementRecordShort + " item" + sfx + " = new " +
                        field.elementRecordShort + "();\n";
                for (SerializedField const& sub : field.elementFields)
                {
                    code += emitSerializedFieldCSharpRead(sub, "item" + sfx, indent + "        ");
                }
                code += indent + "        " + elementRef + " = item" + sfx + ";\n";
            }
            code += indent + "    }\n" + indent + "}\n";
        }
        return code;
    }

    std::string emitSerializedCodecCSharp(SerializedTypeInfo const& info)
    {
        std::string cs;
        cs += "    /// <summary>序列化块编解码：块头三项与 C++ 侧同源常量（字段布局哈希含名+类型+偏移）</summary>\n";
        cs += "    internal static class " + info.shortName + "Codec\n    {\n";
        cs += "        public const uint FormatVersion = 1u;\n";
        cs += "        public const ulong TypeNameHash = " + std::to_string(info.typeHash) + "UL;\n";
        cs += "        public const ulong LayoutHash = " + std::to_string(info.layoutHash) + "UL;\n";
        // 顺序固定为 Serialize → Write → Read → Deserialize：既有共享值类型的逐字判据（字段行
        // 之后必须紧跟 `public static byte[] Serialize(...)` 与块头三行）按这个顺序钉住了。
        cs += "        public static byte[] Serialize(" + info.shortName + " value)\n        {\n";
        cs += "            using (var stream = new System.IO.MemoryStream())\n";
        cs += "            using (var writer = new System.IO.BinaryWriter(stream))\n            {\n";
        cs += "                writer.Write(1u);\n";
        cs += "                writer.Write(TypeNameHash);\n";
        cs += "                writer.Write(LayoutHash);\n";
        cs += "                Write(writer, value);\n";
        cs += "                return stream.ToArray();\n            }\n        }\n";
        // 字段级写/读（不含块头）：嵌套记录直接复用这两个入口（D8 扩展）。
        cs += "        internal static void Write(System.IO.BinaryWriter writer, " + info.shortName +
              " value)\n        {\n";
        for (SerializedField const& field : info.fields)
        {
            cs += emitSerializedFieldCSharpWrite(field, "value", "            ");
        }
        cs += "        }\n";
        cs += "        internal static " + info.shortName +
              " Read(System.IO.BinaryReader reader)\n        {\n";
        cs += "            " + info.shortName + " value = new " + info.shortName + "();\n";
        for (SerializedField const& field : info.fields)
        {
            cs += emitSerializedFieldCSharpRead(field, "value", "            ");
        }
        cs += "            return value;\n        }\n";
        cs += "        /// <summary>从字节还原（块头三项逐项校验；不符即抛 InvalidDataException）</summary>\n";
        cs += "        public static " + info.shortName + " Deserialize(byte[] bytes)\n        {\n";
        cs += "            if (bytes == null) { throw new System.ArgumentNullException(nameof(bytes)); }\n";
        cs += "            using (var stream = new System.IO.MemoryStream(bytes))\n";
        cs += "            using (var reader = new System.IO.BinaryReader(stream))\n            {\n";
        cs += "                if (reader.ReadUInt32() != FormatVersion)\n";
        cs += "                {\n";
        cs += "                    throw new System.IO.InvalidDataException(\"序列化块格式版本不符\");\n";
        cs += "                }\n";
        cs += "                if (reader.ReadUInt64() != TypeNameHash)\n";
        cs += "                {\n";
        cs += "                    throw new System.IO.InvalidDataException(\"序列化块类型名哈希不符\");\n";
        cs += "                }\n";
        cs += "                if (reader.ReadUInt64() != LayoutHash)\n";
        cs += "                {\n";
        cs += "                    throw new System.IO.InvalidDataException(\"序列化块字段布局哈希不符\");\n";
        cs += "                }\n";
        cs += "                return Read(reader);\n            }\n        }\n    }\n\n";
        return cs;
    }

    // ---- 真实头投影快照（任务 4.1）：只解析、不产物的 `--parse-report` ----
    // 每个类一段：`[类] 限定名`，随后每个公开成员函数一行：
    //   `[采集] 类短名::方法名 | 形状：... | 判定：...`
    //   `[排除] 类短名::方法名 | 形状：... | 判定：原因`
    // 独立对账脚本（check_native_snapshot.py）只认 `[采集]/[排除] 类短名::方法名` 的多重集。
    bool parseReportInMainFile(CXCursor cursor, std::string const& mainFile)
    {
        CXFile file = nullptr;
        unsigned line = 0, column = 0, offset = 0;
        clang_getFileLocation(clang_getCursorLocation(cursor), &file, &line, &column, &offset);
        if (file == nullptr)
        {
            return false;
        }
        std::string path = toStd(clang_getFileName(file));
        std::string wanted = mainFile;
        for (char& character : path) { if (character == '\\') { character = '/'; } }
        for (char& character : wanted) { if (character == '\\') { character = '/'; } }
        return !path.empty() && !wanted.empty() &&
               (path == wanted ||
                (path.size() >= wanted.size() &&
                 path.compare(path.size() - wanted.size(), wanted.size(), wanted) == 0));
    }

    struct ReportClassContext
    {
        std::string* out = nullptr;
        std::string shortName;
        // 本批 2.2：快照对账键 = 完整限定名（`[采集] graphics_backend::GPUGraph::AddPass`）。
        std::string qualifiedName;
        bool isStruct = false;
        // 该类是不是 `CA_BIND` 绑定类（本文件里扫出来的标注）。只有绑定类才按新投影规则
        // （D2 内存窗 / D8 记录两段式 / D9 实例化声明）判采集；非绑定类维持旧口径 ——
        // 非绑定类不进绑定面，不为它猜配对（`AddData` / `DataReference::Create` 就属这一类）。
        bool isBound = false;
        // 可序列化记录集合（D8 判定用；由 parseReport 填）。
        std::set<std::string> const* serializableRecords = nullptr;
        // 该类的标注（D9：`CA_BIND_INSTANTIATE` 的模板形态判定用）。
        BoundMarker const* marker = nullptr;
    };

    struct ParseReportState
    {
        std::string* out = nullptr;
        const char* mainFile = nullptr;
        int classCount = 0;
        std::set<std::string> boundNames;
        // 可序列化记录集合（D8：返回位置裸指针能不能走两段式的判定依据）。
        std::set<std::string> serializableRecords;
        // 每个绑定类的标注（D9：显式实例化声明）。
        std::map<std::string, BoundMarker> markersByName;
    };

    std::string reportVerdict(CXCursor cursor, CXCursorKind kind, bool classIsBound,
                              std::set<std::string> const& serializableRecords,
                              BoundMarker const* marker)
    {
        if (kind == CXCursor_FunctionTemplate)
        {
            // D9：写了显式实例化声明的模板形态是采集项（生成器按声明逐形态出槽）。
            if (marker != nullptr)
            {
                auto declared = marker->instantiateTypes.find(toStd(clang_getCursorSpelling(cursor)));
                if (declared != marker->instantiateTypes.end() && !declared->second.empty())
                {
                    std::string types;
                    for (std::string const& type : declared->second)
                    {
                        types += (types.empty() ? "" : ", ") + type;
                    }
                    return "采集：可绑定（模板显式实例化： " + types + " ）";
                }
            }
            return "排除：① 模板方法（本批不做模板单态化）";
        }
        const CXType returnType = clang_getResultType(clang_getCursorType(cursor));
        const std::string methodName = toStd(clang_getCursorSpelling(cursor));
        const std::string returnSpelling =
            toStd(clang_getTypeSpelling(clang_getCanonicalType(returnType)));
        std::vector<std::string> paramSpellings;
        std::vector<std::string> paramNames;
        const int count = clang_Cursor_getNumArguments(cursor);
        for (int index = 0; index < count; ++index)
        {
            const CXCursor argument = clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
            paramSpellings.push_back(
                toStd(clang_getTypeSpelling(clang_getCanonicalType(clang_getCursorType(argument)))));
            paramNames.push_back(toStd(clang_getCursorSpelling(argument)));
        }
        std::string spellings = returnSpelling;
        for (std::string const& spelling : paramSpellings)
        {
            spellings += " " + spelling;
        }
        // 内存窗（任务 1.1 / design D2）：只在绑定类上按相邻唯一整型配对判；配上了就把那对
        // 参数当"已投影"，不再算 ②。配对失败照旧是 ②，并在判定里点名失败原因。
        std::set<std::size_t> windowPointers;
        std::set<std::size_t> windowLengths;
        std::string windowNote;
        bool recordReturn = false;
        if (classIsBound)
        {
            int windowPairs = 0;
            for (std::size_t index = 0; index < paramSpellings.size(); ++index)
            {
                if (!isConstVoidPointerSpelling(paramSpellings[index]))
                {
                    continue;
                }
                std::vector<std::size_t> candidates;
                if (index > 0 && isIntegerSpelling(paramSpellings[index - 1]))
                {
                    candidates.push_back(index - 1);
                }
                if (index + 1 < paramSpellings.size() && isIntegerSpelling(paramSpellings[index + 1]))
                {
                    candidates.push_back(index + 1);
                }
                if (candidates.size() == 1)
                {
                    windowPointers.insert(index);
                    windowLengths.insert(candidates[0]);
                    ++windowPairs;
                    continue;
                }
                windowNote = candidates.empty()
                                 ? "，内存窗配对失败：const void* 参数 " + paramNames[index] +
                                       " 没有相邻整型长度参数"
                                 : "，内存窗配对不唯一：const void* 参数 " + paramNames[index] +
                                       " 两侧有多个整型候选";
            }
            for (std::size_t index = 0; index < paramSpellings.size(); ++index)
            {
                if (windowPointers.count(index) > 0 || windowLengths.count(index) > 0)
                {
                    continue;
                }
                // 注入参数（CA_BIND_INJECT）：该指针由引擎钩子提供，托管表面看不到它 ⇒ 不算 ②。
                if (marker != nullptr)
                {
                    auto injection = marker->injectParams.find(methodName);
                    if (injection != marker->injectParams.end() &&
                        std::find(injection->second.begin(), injection->second.end(), paramNames[index]) !=
                            injection->second.end())
                    {
                        continue;
                    }
                }
                if (paramSpellings[index].find('*') != std::string::npos)
                {
                    return "排除：② 所有权不明的裸指针（参数或返回位置）" + windowNote;
                }
            }
            if (returnSpelling.find('*') != std::string::npos)
            {
                const std::string returnBare = bareType(returnSpelling);
                const bool returnIsVoidPointer = returnBare == "void" &&
                                                 returnSpelling.find("const") == std::string::npos;
                if (!returnIsVoidPointer)
                {
                    // D8：返回位置指向可序列化记录 ⇒ 记录两段式（可绑定）；其它裸指针仍 ②。
                    if (serializableRecords.count(unqualifiedName(returnBare)) > 0)
                    {
                        recordReturn = true;
                    }
                    else
                    {
                        return "排除：② 所有权不明的裸指针（参数或返回位置）";
                    }
                }
            }
            if (!windowNote.empty())
            {
                // 配对失败的形状：判 ②（不进绑定面），并把失败原因写清。
                return "排除：② 所有权不明的裸指针（参数或返回位置）" + windowNote;
            }
            if (recordReturn)
            {
                return "采集：可绑定（记录两段式：托管得序列化只读副本，空命中为 null）";
            }
            if (spellings.find("unordered_map") != std::string::npos ||
                spellings.find("std::map") != std::string::npos ||
                spellings.find("std::set") != std::string::npos ||
                spellings.find("std::array") != std::string::npos)
            {
                return "排除：映射容器 / 定长数组（本批未实现）";
            }
            if (windowPairs > 0)
            {
                return "采集：可绑定（内存窗投影：托管收 byte[]，thunk 以固定内存传指针与长度）";
            }
            if (spellings.find("vector") != std::string::npos ||
                spellings.find("string") != std::string::npos ||
                spellings.find("NameHash") != std::string::npos)
            {
                return "采集：可绑定（容器/文本走序列化块或两段式）";
            }
            return "采集：可绑定";
        }
        if (spellings.find('*') != std::string::npos)
        {
            return "排除：② 所有权不明的裸指针（参数或返回位置）";
        }
        if (spellings.find("unordered_map") != std::string::npos ||
            spellings.find("std::map") != std::string::npos ||
            spellings.find("std::set") != std::string::npos ||
            spellings.find("std::array") != std::string::npos)
        {
            return "排除：映射容器 / 定长数组（本批未实现）";
        }
        if (spellings.find("vector") != std::string::npos ||
            spellings.find("string") != std::string::npos ||
            spellings.find("NameHash") != std::string::npos)
        {
            return "采集：可绑定（容器/文本走序列化块或两段式）";
        }
        return "采集：可绑定";
    }

    CXChildVisitResult collectReportMethods(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* context = static_cast<ReportClassContext*>(data);
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind != CXCursor_CXXMethod && kind != CXCursor_FunctionTemplate)
        {
            return CXChildVisit_Continue;
        }
        const CX_CXXAccessSpecifier access = clang_getCXXAccessSpecifier(cursor);
        if (access != CX_CXXPublic && !(context->isStruct && access == CX_CXXInvalidAccessSpecifier))
        {
            return CXChildVisit_Continue;
        }
        std::string name = toStd(clang_getCursorSpelling(cursor));
        if (name.empty() || name[0] == '~' || name.rfind("operator", 0) == 0)
        {
            return CXChildVisit_Continue;
        }
        std::string shape;
        if (kind == CXCursor_CXXMethod)
        {
            shape = toStd(clang_getTypeSpelling(clang_getResultType(clang_getCursorType(cursor))));
            shape += " " + name + "(";
            const int count = clang_Cursor_getNumArguments(cursor);
            for (int index = 0; index < count; ++index)
            {
                if (index > 0)
                {
                    shape += ", ";
                }
                const CXCursor argument =
                    clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
                shape += toStd(clang_getTypeSpelling(clang_getCursorType(argument)));
            }
            shape += ")";
            if (clang_CXXMethod_isStatic(cursor) != 0u)
            {
                shape = "static " + shape;
            }
        }
        else
        {
            shape = "template<> " + name + "(...)";
        }
        static std::set<std::string> const emptySerializableRecords;
        const std::string verdict =
            reportVerdict(cursor, kind, context->isBound,
                          context->serializableRecords != nullptr ? *context->serializableRecords
                                                                  : emptySerializableRecords,
                          context->marker);
        const char* prefix = verdict.rfind("采集", 0) == 0 ? "采集" : "排除";
        *context->out += std::string("[") + prefix + "] " + context->qualifiedName + "::" + name +
                         " | 形状：" + shape + " | 判定：" + verdict + "\n";
        return CXChildVisit_Continue;
    }

    CXChildVisitResult collectReportClasses(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* state = static_cast<ParseReportState*>(data);
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind != CXCursor_ClassDecl && kind != CXCursor_StructDecl)
        {
            return CXChildVisit_Recurse;
        }
        if (clang_isCursorDefinition(cursor) == 0 ||
            !parseReportInMainFile(cursor, state->mainFile))
        {
            return CXChildVisit_Recurse;
        }
        if (!clang_Cursor_isNull(clang_getSpecializedCursorTemplate(cursor)))
        {
            return CXChildVisit_Recurse; // 模板特化：两侧都不采
        }
        // 模板类（ClassTemplate）不以 ClassDecl 形态到这里；这里再挡一遍显式特化。
        ReportClassContext context;
        context.out = state->out;
        context.shortName = toStd(clang_getCursorSpelling(cursor));
        context.qualifiedName = qualifiedCursorName(cursor);
        context.isStruct = kind == CXCursor_StructDecl;
        context.isBound = state->boundNames.count(context.shortName) > 0;
        context.serializableRecords = &state->serializableRecords;
        {
            auto foundMarker = state->markersByName.find(context.shortName);
            context.marker = foundMarker == state->markersByName.end() ? nullptr : &foundMarker->second;
        }
        *state->out += "[类] " + qualifiedCursorName(cursor) + "\n";
        clang_visitChildren(cursor, collectReportMethods, &context);
        ++state->classCount;
        return CXChildVisit_Recurse;
    }

    int parseReport(const char* filePath, const std::vector<const char*>& clangArgs,
                    std::string const& outPath)
    {
        std::vector<const char*> args;
        args.push_back("-std=c++20");
        args.push_back("-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH");
        for (const char* argument : clangArgs)
        {
            args.push_back(argument);
        }
        CXIndex index = clang_createIndex(1, 0);
        CXTranslationUnit unit = nullptr;
        const CXErrorCode error = clang_parseTranslationUnit2(
            index, filePath, args.data(), static_cast<int>(args.size()), nullptr, 0,
            CXTranslationUnit_DetailedPreprocessingRecord, &unit);
        if (error != CXError_Success || unit == nullptr)
        {
            std::fprintf(stderr, "CodeGen: --parse-report 解析失败（error=%d）：%s\n",
                         static_cast<int>(error), filePath);
            clang_disposeIndex(index);
            return 3;
        }
        unsigned errorCount = 0;
        const unsigned diagnosticCount = clang_getNumDiagnostics(unit);
        for (unsigned i = 0; i < diagnosticCount; ++i)
        {
            CXDiagnostic diagnostic = clang_getDiagnostic(unit, i);
            if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error)
            {
                CXString text = clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions());
                std::fprintf(stderr, "  libclang: %s\n", clang_getCString(text));
                clang_disposeString(text);
                ++errorCount;
            }
            clang_disposeDiagnostic(diagnostic);
        }
        if (errorCount > 0)
        {
            clang_disposeTranslationUnit(unit);
            clang_disposeIndex(index);
            std::fprintf(stderr, "CodeGen: --parse-report 看到 %u 条错误诊断，拒绝输出快照。\n",
                         errorCount);
            return 3;
        }
        std::string report;
        report += "# 原生头投影快照（codegen-native-binding-prereqs 任务 4.1）\n";
        report += "# 生成器：CodeGen.exe <头> --parse-report\n";
        std::string normalized = filePath;
        for (char& character : normalized)
        {
            if (character == '\\')
            {
                character = '/';
            }
        }
        report += "[头] " + normalized + "\n";
        ParseReportState state;
        state.out = &report;
        state.mainFile = filePath;
        // 绑定类清单：本文件文本里的 `CA_BIND` 标注。只有绑定类才走新投影规则，
        // 非绑定类维持旧口径（不为不进绑定面的类猜内存窗配对）。
        for (BoundMarker const& marker : scanBoundMarkers(readWholeFile(filePath)))
        {
            state.boundNames.insert(marker.interfaceName);
            state.markersByName[marker.interfaceName] = marker;
        }
        // 可序列化记录集合（D8）：走序列化块路径的记录类型才能当"记录两段式"的返回目标。
        {
            std::map<std::string, CXCursor> reportRecords;
            StructCursorCollector reportCollector;
            reportCollector.sink = &reportRecords;
            clang_visitChildren(clang_getTranslationUnitCursor(unit), collectStructCursorsForMirror,
                                &reportCollector);
            for (auto const& entry : reportRecords)
            {
                if (recordNeedsSerialization(entry.second))
                {
                    state.serializableRecords.insert(entry.first);
                }
            }
        }
        clang_visitChildren(clang_getTranslationUnitCursor(unit), collectReportClasses, &state);
        clang_disposeTranslationUnit(unit);
        clang_disposeIndex(index);
        if (outPath.empty())
        {
            std::fputs(report.c_str(), stdout);
        }
        else
        {
            std::ofstream out(outPath, std::ios::binary);
            out << report;
            if (!out)
            {
                std::fprintf(stderr, "CodeGen: --parse-report 写不了 '%s'\n", outPath.c_str());
                return 2;
            }
        }
        std::fprintf(stderr, "快照：%d 个类\n", state.classCount);
        return 0;
    }

    // =========================================================================================
    // 反向投影发射（csharp-reverse-interface-binding）：C# 实现 C++ 接口
    //
    // 三类产物（design D1/D2/D4/D6/D9）：
    //   <线形>.Bindings.cpp —— C++ 代理 <线形>Proxy（每被绑定虚方法调一条函数指针槽）、注册 glue
    //                          __CreateImpl / __CreateProxy / __ReleaseImpl / __ReverseKeepAliveCount、
    //                          反向函数表 <线形>Api_v2 与 GetApi（登记表路由用）、共用句柄表的借用项。
    //   <线形>.Bindings.cs  —— 公开抽象基类（去 I 前缀加 Base）+ internal 托管桥（GCHandle 保活、
    //                          唯一一次 Free 在托管侧由 releaseUser 触发、公开签名里没有原生细节）。
    //   ReverseBindingRuntime.h —— 所有反向产物共用的错误类型与保活状态（同一次生成只写一份）。
    //
    // 支持的反向方法形状（其余形状生成期拒绝并点名，不做静默降级）：
    //   ① void / 标量返回 + 纯标量参数
    //   ② `castl::string` 返回（两段式 <方法>Len + Copy<方法>）
    //   ③ `void Serialize(ca_io::WBatch*)`（写路径：SerializeLen + SerializeCopy，C# 侧返回 byte[]）
    //   ④ 返回另一个**反向**接口的裸指针（C# 侧返回该接口的抽象基类，C ABI 槽传 void*）
    // 方法级 `CA_BIND_SKIP` 跳过的纯虚方法在代理里出一份抛异常的体（不改接口签名）。
    // =========================================================================================
    struct ReverseScalarSpec
    {
        char const* canonical;
        char const* cppType;
        char const* csType;
        char const* csNative;
    };

    ReverseScalarSpec const kReverseScalarSpecs[] = {
        { "int32_t", "int32_t", "int", "int" },
        { "int64_t", "int64_t", "long", "long" },
        { "uint32_t", "uint32_t", "uint", "uint" },
        { "uint64_t", "uint64_t", "ulong", "ulong" },
        { "float", "float", "float", "float" },
        { "double", "double", "double", "double" },
        { "bool", "int32_t", "bool", "int" },
    };

    ReverseScalarSpec const* findReverseScalar(std::string const& spelling)
    {
        const std::string canonical = normalizeScalarSpelling(spelling);
        if (canonical.empty())
        {
            return nullptr;
        }
        for (ReverseScalarSpec const& spec : kReverseScalarSpecs)
        {
            if (canonical == spec.canonical)
            {
                return &spec;
            }
        }
        return nullptr;
    }

    bool reverseSpecIsBool(ReverseScalarSpec const* spec)
    {
        return spec != nullptr && std::string(spec->canonical) == "bool";
    }

    std::string reverseBaseName(std::string const& shortName)
    {
        std::string bare = shortName;
        if (bare.size() > 1 && bare[0] == 'I' && bare[1] >= 'A' && bare[1] <= 'Z')
        {
            bare = bare.substr(1);
        }
        return bare + "Base";
    }

    std::string joinWith(std::vector<std::string> const& parts, char const* separator)
    {
        std::string out;
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (i != 0)
            {
                out += separator;
            }
            out += parts[i];
        }
        return out;
    }

    struct ReverseMethodPlan
    {
        MethodSignature const* method = nullptr;
        // 0 = void / 1 = 标量 / 2 = 字符串 / 3 = 写路径（Serialize(WBatch*)）/ 4 = 反向对象指针
        int kind = -1;
        ReverseScalarSpec const* retScalar = nullptr;
        std::vector<ReverseScalarSpec const*> params;
        // 返回位置指向的另一个反向接口（只存名字：它可能不在**本次**生成输入里 ——
        // IResourceFactory.h include 了 IResource.h，但主头里的反向标注只有 IResourceFactory）。
        std::string objectTargetQualified;
        std::string objectTargetShort;
        std::vector<std::string> slotDecls;    // `uint64_t (*SerializeLen)(void* user)`
        std::vector<std::string> slotCsTypes;  // `delegate* unmanaged<void*, ulong>`
        std::vector<std::string> paramNames;
    };

    std::vector<std::string> allSlotNames(std::vector<ReverseMethodPlan> const& plans)
    {
        std::vector<std::string> names;
        for (ReverseMethodPlan const& plan : plans)
        {
            for (std::string const& decl : plan.slotDecls)
            {
                const std::size_t star = decl.find("(*");
                const std::size_t close = decl.find(')', star);
                names.push_back(decl.substr(star + 2, close - star - 2));
            }
        }
        return names;
    }

    // C# 托管桥的参数声明（`int p0`；bool 参数在 C ABI 上是 int）。
    std::vector<std::string> reverseCsSlotParamDecls(ReverseMethodPlan const& plan)
    {
        std::vector<std::string> params;
        for (std::size_t index = 0; index < plan.params.size(); ++index)
        {
            params.push_back(std::string(plan.params[index]->csNative) + " " + plan.paramNames[index]);
        }
        return params;
    }

    // C# 托管桥调抽象方法时的实参（bool 参数从 int 转回 bool）。
    std::vector<std::string> reverseCsCallArgs(ReverseMethodPlan const& plan)
    {
        std::vector<std::string> args;
        for (std::size_t index = 0; index < plan.params.size(); ++index)
        {
            args.push_back(reverseSpecIsBool(plan.params[index])
                               ? (plan.paramNames[index] + " != 0")
                               : plan.paramNames[index]);
        }
        return args;
    }

    int emitReverseRuntime(std::string const& outDir)
    {
        std::string text;
        text += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
        text += "// 反向接口（C# 实现 C++）的共用运行时：错误类型与保活状态。所有反向产物都包含它。\n";
        text += "#pragma once\n";
        text += "#include <atomic>\n#include <cstdint>\n#include <cstdlib>\n#include <stdexcept>\n#include <string>\n\n";
        text += "namespace cascaded_reverse\n{\n";
        text += "    // 反向接口的失败（实现抛异常 / 非注册线程 / 返回值不可用 / 不支持的反向读）。\n";
        text += "    // `Code()` 是统一错误表里的错误码（1001 句柄失效 / 1002 空指针 / 1005 非注册线程 /\n";
        text += "    // 1012 反向实现异常 / 1013 不支持的反向读）；`what()` 同时含反向标识与原始文本。\n";
        // 跨模块硬约束（实测两次）：异常对象在**抛出模块**（CSharpHost.dll 的代理）里构造、在**调用方
        // 模块**（tester.exe）里被 catch。这时**不能**读本类自己新增的成员 / 新增的虚函数槽：
        //   ① 非虚 inline `Code()` 读 `m_Code` —— 两侧布局只要有偏差就读到错误偏移（1013 读成 1）；
        //   ② 把 `Code()` 改成虚函数 —— catch 侧按自己的 vtable 假设取槽，旧 vtable 上那格是空，Rip=0 崩。
        // 唯一跨模块稳定的通道是 `what()`（`std::runtime_error` 的虚函数，槽位由 CRT 固定）。所以：
        // 错误码**写进 what() 的固定前缀 `[码] `**，`Code()` 只解析 `what()`，不碰任何派生成员。
        text += "    class ReverseBindingError : public std::runtime_error\n    {\n    public:\n";
        text += "        ReverseBindingError(int32_t code, std::string const& message)\n";
        text += "            : std::runtime_error(ComposeMessage(code, message)) {}\n";
        text += "        // 只读 `what()` 的前缀（跨模块稳定），不读任何派生成员。格式对不上就返回 0（= 未知）。\n";
        text += "        int32_t Code() const noexcept\n        {\n";
        text += "            char const* text = what();\n";
        text += "            if (text == nullptr || text[0] != '[') { return 0; }\n";
        text += "            return static_cast<int32_t>(std::strtol(text + 1, nullptr, 10));\n";
        text += "        }\n";
        text += "    private:\n";
        text += "        static std::string ComposeMessage(int32_t code, std::string const& message)\n";
        text += "        {\n";
        text += "            return \"[\" + std::to_string(code) + \"] \" + message;\n";
        text += "        }\n";
        text += "    };\n\n";
        text += "    // 每个反向实例一个保活计数（不是全局静态：同进程里多个反向实例各数各的）。\n";
        text += "    struct KeepAlive\n    {\n        std::atomic<uint32_t> count{1u};\n    };\n}\n";
        const std::string path = outDir + "/ReverseBindingRuntime.h";
        std::ofstream out(path, std::ios::binary);
        out << text;
        if (!out)
        {
            std::fprintf(stderr, "CodeGen: cannot write '%s'\n", path.c_str());
            return 2;
        }
        return 0;
    }

    int emitReverseProducts(InterfaceShape const& shape, BoundMarker const& marker,
                            std::map<std::string, InterfaceShape const*> const& reverseByShort,
                            std::set<std::string> const& reverseKnown,
                            char const* filePath, std::size_t handleIndexBits,
                            std::size_t handleGenerationBits, std::string const& outDir,
                            int* emittedMethods)
    {
        const std::string linear = qualifiedLinearName(shape.qualifiedName);
        const std::string baseName = reverseBaseName(shape.name);
        const std::string csNamespace = cSharpNamespaceOf(shape.qualifiedName);

        // ---- 1. 方法分类：被绑定虚方法 / 跳过的方法（形状不支持 = 生成期拒绝）----
        std::vector<ReverseMethodPlan> plans;
        std::vector<MethodSignature const*> skippedPure;
        std::set<std::string> usedSlotBases;
        int rejected = 0;
        for (MethodSignature const& method : shape.methods)
        {
            if (method.isStatic || method.isTemplate)
            {
                continue;
            }
            auto skip = marker.skipReasons.find(method.name);
            if (skip != marker.skipReasons.end())
            {
                if (method.isPure)
                {
                    skippedPure.push_back(&method);
                }
                continue;
            }
            if (!method.isVirtual)
            {
                std::fprintf(stderr,
                             "CodeGen: 反向 %s —— 跳过 %s（非 virtual，C# 无法覆写，不投影）\n",
                             shape.qualifiedName.c_str(), method.name.c_str());
                continue;
            }
            // 同名重载会让槽名 / 托管桥方法名撞车（C++ struct 成员重定义、C# 方法重复）：
            // 反向投影不支持重载，生成期点名拒绝，不做静默改名。
            if (!usedSlotBases.insert(method.name).second)
            {
                std::fprintf(stderr,
                             "CodeGen: 拒绝反向 %s::%s —— 同名重载不在反向投影支持的范围里\n",
                             shape.qualifiedName.c_str(), method.name.c_str());
                ++rejected;
                continue;
            }
            ReverseMethodPlan plan;
            plan.method = &method;
            // 写路径要**先于参数校验**分流：`ca_io::WBatch*` 不是可投影的标量参数，但它正是
            // "反向资源写进批"唯一的那条入口（C# 侧是 `byte[] Serialize()`，两段式 Len/Copy）。
            if (method.name == "Serialize" && bareType(method.returnSpelling) == "void" &&
                method.params.size() == 1 &&
                method.params[0].first.find("WBatch") != std::string::npos &&
                method.params[0].first.find('*') != std::string::npos)
            {
                plan.kind = 3;
                plan.slotDecls.push_back("uint64_t (*SerializeLen)(void* user)");
                plan.slotCsTypes.push_back("delegate* unmanaged<void*, ulong>");
                plan.slotDecls.push_back("uint64_t (*SerializeCopy)(void* user, void* dst, uint64_t cap)");
                plan.slotCsTypes.push_back("delegate* unmanaged<void*, void*, ulong, ulong>");
                plans.push_back(std::move(plan));
                continue;
            }
            bool paramsOk = true;
            for (std::size_t index = 0; index < method.params.size(); ++index)
            {
                ReverseScalarSpec const* scalar = findReverseScalar(method.params[index].first);
                if (scalar == nullptr)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝反向 %s::%s —— 参数 %zu 的形状 `%s` 不在反向投影支持的形状里"
                                 "（只支持标量参数；其它形状要另立批并在 README 登记）\n",
                                 shape.qualifiedName.c_str(), method.name.c_str(), index + 1,
                                 method.params[index].first.c_str());
                    ++rejected;
                    paramsOk = false;
                    break;
                }
                plan.params.push_back(scalar);
                plan.paramNames.push_back(method.params[index].second.empty()
                                              ? "p" + std::to_string(index)
                                              : method.params[index].second);
            }
            if (!paramsOk)
            {
                continue;
            }
            const std::string returnBare = bareType(method.returnSpelling);
            const bool returnsVoid = returnBare == "void";
            const bool returnsString =
                method.returnSpelling.find("castl::string") != std::string::npos ||
                method.returnSpelling.find("std::string") != std::string::npos;
            if (returnsString)
            {
                // 两段式字符串返回的 Len 槽只带 user（与正向 `GetWindowNameLen` 同形）：带参数的方法
                // 没地方传参，生成期点名拒绝（不做静默丢参）。
                if (!method.params.empty())
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝反向 %s::%s —— 返回 castl::string 的方法本批只支持无参"
                                 "（两段式 Len 槽只带 user）\n",
                                 shape.qualifiedName.c_str(), method.name.c_str());
                    ++rejected;
                    continue;
                }
                plan.kind = 2;
                plan.slotDecls.push_back("uint64_t (*" + method.name + "Len)(void* user)");
                plan.slotCsTypes.push_back("delegate* unmanaged<void*, ulong>");
                plan.slotDecls.push_back("uint64_t (*Copy" + method.name +
                                         ")(void* user, char* dst, uint64_t cap)");
                plan.slotCsTypes.push_back("delegate* unmanaged<void*, byte*, ulong, ulong>");
                plans.push_back(std::move(plan));
                continue;
            }
            if (returnsVoid)
            {
                plan.kind = 0;
            }
            else if (ReverseScalarSpec const* scalar = findReverseScalar(method.returnSpelling))
            {
                plan.kind = 1;
                plan.retScalar = scalar;
            }
            else
            {
                // 返回位置指向另一个反向接口：C# 侧拿到的就是那个接口的抽象基类。
                // 目标可能不在**本次**生成输入里（IResourceFactory.h include 了 IResource.h，但主头里
                // 只有 IResourceFactory 的标注）：这时靠"包含闭包里也出现过 CA_BIND_REVERSE"来认它，
                // C# 命名空间与基类名都只由完整限定名决定，不需要完整形状。
                std::string targetQualified;
                std::string targetShort;
                if (method.returnSpelling.find('*') != std::string::npos)
                {
                    const std::size_t split = returnBare.rfind("::");
                    const std::string shortName =
                        split == std::string::npos ? returnBare : returnBare.substr(split + 2);
                    auto found = reverseByShort.find(shortName);
                    if (found != reverseByShort.end() &&
                        (split == std::string::npos || found->second->qualifiedName == returnBare))
                    {
                        targetQualified = found->second->qualifiedName;
                        targetShort = found->second->name;
                    }
                    else if (reverseKnown.count(shortName) > 0)
                    {
                        // 目标不在本次输入里：按 `--known-interface` 清单认，优先同命名空间的那条
                        // （同短名跨命名空间时不许瞎猜）。
                        const std::string prefix = shape.qualifiedName.rfind("::") == std::string::npos
                                                       ? std::string()
                                                       : shape.qualifiedName.substr(
                                                             0, shape.qualifiedName.rfind("::") + 2);
                        std::vector<std::pair<std::string, std::string>> candidates;
                        for (auto const& entry : g_knownInterfaces)
                        {
                            if (entry.first == shortName)
                            {
                                candidates.push_back(entry);
                            }
                        }
                        if (candidates.size() == 1)
                        {
                            targetQualified = candidates[0].second;
                            targetShort = shortName;
                        }
                        else
                        {
                            for (auto const& candidate : candidates)
                            {
                                if (!prefix.empty() && candidate.second.rfind(prefix, 0) == 0)
                                {
                                    targetQualified = candidate.second;
                                    targetShort = shortName;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (targetQualified.empty())
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝反向 %s::%s —— 返回类型 `%s` 不在反向投影支持的形状里"
                                 "（标量 / castl::string / 另一个**反向**接口的裸指针）\n",
                                 shape.qualifiedName.c_str(), method.name.c_str(),
                                 method.returnSpelling.c_str());
                    ++rejected;
                    continue;
                }
                plan.kind = 4;
                plan.objectTargetQualified = targetQualified;
                plan.objectTargetShort = targetShort;
            }
            std::vector<std::string> slotParams;
            slotParams.push_back("void* user");
            for (std::size_t index = 0; index < plan.params.size(); ++index)
            {
                slotParams.push_back(std::string(plan.params[index]->cppType) + " " + plan.paramNames[index]);
            }
            std::string slotReturn;
            if (plan.kind == 0)
            {
                slotReturn = "void";
            }
            else if (plan.kind == 1)
            {
                slotReturn = plan.retScalar->cppType;
            }
            else
            {
                slotReturn = "void*";
            }
            plan.slotDecls.push_back(slotReturn + " (*" + method.name + ")(" +
                                     joinWith(slotParams, ", ") + ")");
            std::string csSlotReturn;
            if (plan.kind == 0)
            {
                csSlotReturn = "void";
            }
            else if (plan.kind == 1)
            {
                csSlotReturn = plan.retScalar->csNative;
            }
            else
            {
                csSlotReturn = "void*";
            }
            std::vector<std::string> csSlotParams;
            csSlotParams.push_back("void*");
            for (ReverseScalarSpec const* param : plan.params)
            {
                csSlotParams.push_back(param->csNative);
            }
            csSlotParams.push_back(csSlotReturn);
            plan.slotCsTypes.push_back("delegate* unmanaged<" + joinWith(csSlotParams, ", ") + ">");
            plans.push_back(std::move(plan));
        }
        if (rejected > 0)
        {
            return 1;
        }
        if (emittedMethods != nullptr)
        {
            *emittedMethods = static_cast<int>(plans.size());
        }

        // 错误通道槽（所有反向接口共用）：取实现侧的异常文本。
        const std::string takeErrorDecl = "uint64_t (*TakeLastError)(void* user, char* dst, uint64_t cap)";
        const std::string takeErrorCs = "delegate* unmanaged<void*, byte*, ulong, ulong>";
        std::vector<std::string> allSlotDecls;
        std::vector<std::string> allSlotCs;
        for (ReverseMethodPlan const& plan : plans)
        {
            for (std::size_t index = 0; index < plan.slotDecls.size(); ++index)
            {
                allSlotDecls.push_back(plan.slotDecls[index]);
                allSlotCs.push_back(plan.slotCsTypes[index]);
            }
        }
        allSlotDecls.push_back(takeErrorDecl);
        allSlotCs.push_back(takeErrorCs);
        const std::string allSlotDeclText = joinWith(allSlotDecls, ", ");
        const std::string allSlotCsText = joinWith(allSlotCs, ", ");

        // ---- 2. C++ 产物 ----
        std::string cpp;
        cpp += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
        cpp += "// CODEGEN-REVERSE-BINDING: " + shape.qualifiedName + "\n";
        cpp += "#pragma once\n";
        cpp += "#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n#include <string.h>\n";
        cpp += "#include <atomic>\n#include <exception>\n#include <memory>\n#include <mutex>\n";
        cpp += "#include <string>\n#include <thread>\n#include <vector>\n";
        {
            std::string base = filePath != nullptr ? std::string(filePath) : std::string();
            const std::size_t slash = base.find_last_of("/\\");
            if (slash != std::string::npos)
            {
                base = base.substr(slash + 1);
            }
            cpp += "#include \"" + base + "\"\n";
        }
        cpp += "#include \"BindingHandleTable.h\"\n";
        cpp += "#include \"ReverseBindingRuntime.h\"\n\n";
        // 注册 glue 的前置声明：既是跨翻译单元可见的入口，也是期望签名对拍（expected.txt 的反向
        // 矩阵行）读的那条 `extern "C" ...;` 声明 —— 只给定义（带函数体）脚本读不到。
        cpp += "extern \"C\" uint64_t " + linear + "__CreateImpl(void* user, " + allSlotDeclText +
               ", void (*releaseUser)(void* user));\n";
        cpp += "extern \"C\" void* " + linear + "__CreateProxy(void* user, " + allSlotDeclText +
               ", void (*releaseUser)(void* user), uint64_t* outHandle);\n";
        cpp += "extern \"C\" int32_t " + linear + "__ReleaseImpl(uint64_t handle);\n";
        cpp += "extern \"C\" void* " + linear + "__ResolveImpl(uint64_t handle);\n";
        cpp += "extern \"C\" uint64_t " + linear + "__ReverseKeepAliveCount(uint64_t handle);\n\n";
        // 错误码：与工具的 kBindErrorCodes / check_bindings.py 的 ERROR_CODE_TABLE / README 同步。
        cpp += "static const int32_t " + linear + "_ERROR_STALE_HANDLE = 1001;\n";
        cpp += "static const int32_t " + linear + "_ERROR_NULL_POINTER = 1002;\n";
        cpp += "static const int32_t " + linear + "_ERROR_CALLBACK_WRONG_THREAD = 1005;\n";
        cpp += "static const int32_t " + linear + "_ERROR_REVERSE_EXCEPTION = 1012;\n";
        cpp += "static const int32_t " + linear + "_ERROR_REVERSE_READ_UNSUPPORTED = 1013;\n\n";
        cpp += "static const uint32_t " + linear + "_HANDLE_INDEX_BITS = " +
               std::to_string(handleIndexBits) + "u;\n";
        cpp += "static const uint32_t " + linear + "_HANDLE_GEN_BITS = " +
               std::to_string(handleGenerationBits) + "u;\n";
        cpp += "// 本接口在共用句柄表里的标签（一个本翻译单元独有的静态对象地址）。\n";
        cpp += "static int const " + linear + "_HANDLE_TAG = 0;\n";
        cpp += "static std::vector<cascaded_bindings::HandleEntry>& " + linear +
               "_handles = cascaded_bindings::HandleTable();\n";
        cpp += "static std::mutex " + linear + "_keepAliveMutex;\n";
        cpp += "static std::vector<std::shared_ptr<cascaded_reverse::KeepAlive>> " + linear +
               "_keepAlives;\n\n";
        cpp += "static uint64_t " + linear + "_claimSlotLocked()\n{\n";
        cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
        cpp += "    uint64_t index = 1u;\n";
        cpp += "    while (index <= indexMask && index < " + linear + "_handles.size() + 1u)\n    {\n";
        cpp += "        if (" + linear + "_handles[index - 1u].kind == 0u) { break; }\n        ++index;\n    }\n";
        cpp += "    if (index > indexMask) { return 0u; }\n";
        cpp += "    if (index > " + linear + "_handles.size()) { " + linear + "_handles.push_back({}); }\n";
        cpp += "    return index;\n}\n";
        cpp += "static uint64_t " + linear + "_handleFromIndex(uint64_t index)\n{\n";
        cpp += "    const uint64_t generation = " + linear + "_handles[index - 1u].generation;\n";
        cpp += "    return (generation << " + linear + "_HANDLE_INDEX_BITS) | index;\n}\n";
        cpp += "static void " + linear +
               "_noteKeepAlive(uint64_t handle, std::shared_ptr<cascaded_reverse::KeepAlive> state)\n{\n";
        cpp += "    const uint64_t index = handle & ((1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull);\n";
        cpp += "    if (index == 0u) { return; }\n";
        cpp += "    std::lock_guard<std::mutex> guard(" + linear + "_keepAliveMutex);\n";
        cpp += "    if (" + linear + "_keepAlives.size() < static_cast<std::size_t>(index)) "
               "{ " + linear + "_keepAlives.resize(static_cast<std::size_t>(index)); }\n";
        cpp += "    " + linear + "_keepAlives[static_cast<std::size_t>(index - 1u)] = std::move(state);\n}\n";
        // 强句柄：表项持 shared_ptr（create 到注册之间是唯一所有者）；注册表经 TakeOwnership 接走。
        cpp += "static uint64_t " + linear +
               "_registerStrongHandle(void* proxy, std::shared_ptr<void> owner)\n{\n";
        cpp += "    std::lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex());\n";
        cpp += "    const uint64_t index = " + linear + "_claimSlotLocked();\n";
        cpp += "    if (index == 0u) { return 0u; }\n";
        cpp += "    cascaded_bindings::HandleEntry& entry = " + linear + "_handles[index - 1u];\n";
        cpp += "    entry.object = proxy;\n    entry.kind = 1u;\n";
        cpp += "    entry.interfaceTag = &" + linear + "_HANDLE_TAG;\n";
        cpp += "    entry.issuerType = \"" + shape.qualifiedName + "\";\n";
        cpp += "    entry.targetType = nullptr;\n    entry.hasWitness = false;\n";
        cpp += "    entry.witnessHandle = 0u;\n    entry.strong = std::move(owner);\n";
        cpp += "    entry.weak.reset();\n";
        cpp += "    return " + linear + "_handleFromIndex(index);\n}\n";
        // 借用句柄：只存裸指针、不持有强引用（资源/工厂代理的唯一所有者在注册表）。
        cpp += "static uint64_t " + linear + "_registerBorrowHandle(void* proxy)\n{\n";
        cpp += "    std::lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex());\n";
        cpp += "    const uint64_t index = " + linear + "_claimSlotLocked();\n";
        cpp += "    if (index == 0u) { return 0u; }\n";
        cpp += "    cascaded_bindings::HandleEntry& entry = " + linear + "_handles[index - 1u];\n";
        cpp += "    entry.object = proxy;\n    entry.kind = 4u;\n";
        cpp += "    entry.interfaceTag = &" + linear + "_HANDLE_TAG;\n";
        cpp += "    entry.issuerType = \"" + shape.qualifiedName + "\";\n";
        cpp += "    entry.targetType = nullptr;\n    entry.hasWitness = false;\n";
        cpp += "    entry.witnessHandle = 0u;\n    entry.strong.reset();\n    entry.weak.reset();\n";
        cpp += "    return " + linear + "_handleFromIndex(index);\n}\n";
        // 取走强引用（单一所有权移交给注册表）；表项降级成借用项，C# 侧句柄仍可用。
        cpp += "static std::shared_ptr<void> " + linear + "_takeStrongHandle(uint64_t handle)\n{\n";
        cpp += "    std::lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex());\n";
        cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
        cpp += "    const uint64_t index = handle & indexMask;\n";
        cpp += "    const uint64_t generation = handle >> " + linear + "_HANDLE_INDEX_BITS;\n";
        cpp += "    if (index == 0u || index > " + linear + "_handles.size()) { return {}; }\n";
        cpp += "    cascaded_bindings::HandleEntry& entry = " + linear + "_handles[index - 1u];\n";
        cpp += "    if (entry.kind != 1u || entry.generation != generation || "
               "entry.interfaceTag != &" + linear + "_HANDLE_TAG) { return {}; }\n";
        cpp += "    std::shared_ptr<void> taken = entry.strong;\n";
        cpp += "    entry.strong.reset();\n    entry.kind = 4u;\n";
        cpp += "    return taken;\n}\n";
        cpp += "static int32_t " + linear + "_retireHandle(uint64_t handle)\n{\n";
        cpp += "    std::lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex());\n";
        cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
        cpp += "    const uint64_t index = handle & indexMask;\n";
        cpp += "    const uint64_t generation = handle >> " + linear + "_HANDLE_INDEX_BITS;\n";
        cpp += "    if (index == 0u || index > " + linear + "_handles.size()) { return " +
               linear + "_ERROR_STALE_HANDLE; }\n";
        cpp += "    cascaded_bindings::HandleEntry& entry = " + linear + "_handles[index - 1u];\n";
        cpp += "    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation || "
               "entry.interfaceTag != &" + linear + "_HANDLE_TAG) { return " +
               linear + "_ERROR_STALE_HANDLE; }\n";
        cpp += "    entry.kind = 0u;\n    entry.object = nullptr;\n";
        cpp += "    entry.strong.reset();\n    entry.weak.reset();\n";
        cpp += "    entry.hasWitness = false;\n    entry.witnessHandle = 0u;\n";
        cpp += "    entry.issuerType = nullptr;\n    entry.targetType = nullptr;\n";
        cpp += "    const uint32_t nextGeneration = (entry.generation + 1u) & ((1u << " + linear +
               "_HANDLE_GEN_BITS) - 1u);\n";
        cpp += "    if (nextGeneration == 0u) { entry.kind = 3u; }\n";
        cpp += "    entry.generation = nextGeneration;\n    return 0;\n}\n\n";
        // 原生 getter：按句柄序号读该实例的保活计数（UINT64_MAX = 这个序号没有实例）。
        cpp += "extern \"C\" uint64_t " + linear + "__ReverseKeepAliveCount(uint64_t handle)\n{\n";
        cpp += "    const uint64_t index = handle & ((1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull);\n";
        cpp += "    std::lock_guard<std::mutex> guard(" + linear + "_keepAliveMutex);\n";
        cpp += "    if (index == 0u || index > " + linear + "_keepAlives.size()) { return UINT64_MAX; }\n";
        cpp += "    std::shared_ptr<cascaded_reverse::KeepAlive> const& state = " + linear +
               "_keepAlives[static_cast<std::size_t>(index - 1u)];\n";
        cpp += "    if (!state) { return UINT64_MAX; }\n";
        cpp += "    return state->count.load();\n}\n\n";

        // ---- 代理类 ----
        cpp += "// 反向代理：每个被绑定虚方法调对应函数指针槽；析构调 releaseUser（唯一一次 GCHandle.Free\n";
        cpp += "// 发生在托管桥里），并让 C# 借用的那个句柄失效。\n";
        cpp += "class " + linear + "Proxy final : public " + shape.qualifiedName + "\n{\npublic:\n";
        cpp += "    struct Slots\n    {\n";
        for (std::string const& decl : allSlotDecls)
        {
            cpp += "        " + decl + " = nullptr;\n";
        }
        cpp += "    };\n\n";
        cpp += "    " + linear + "Proxy(void* user, " + allSlotDeclText + ", void (*releaseUser)(void* user))\n";
        cpp += "        : m_user(user), m_releaseUser(releaseUser),\n";
        cpp += "          m_keepAlive(std::make_shared<cascaded_reverse::KeepAlive>()),\n";
        cpp += "          m_registrationThread(std::this_thread::get_id())\n    {\n";
        {
            std::size_t slotIndex = 0;
            for (ReverseMethodPlan const& plan : plans)
            {
                for (std::size_t index = 0; index < plan.slotDecls.size(); ++index)
                {
                    // 槽名 = 声明里 `(*名字)` 那个名字
                    const std::string& decl = plan.slotDecls[index];
                    const std::size_t star = decl.find("(*");
                    const std::size_t close = decl.find(')', star);
                    const std::string name = decl.substr(star + 2, close - star - 2);
                    cpp += "        m_slots." + name + " = " + name + ";\n";
                    ++slotIndex;
                }
            }
        }
        cpp += "        m_slots.TakeLastError = TakeLastError;\n";
        cpp += "    }\n\n";
        cpp += "    ~" + linear + "Proxy() override\n    {\n";
        cpp += "        if (m_keepAlive) { m_keepAlive->count.store(0u); }\n";
        cpp += "        if (m_borrowHandle != 0u) { " + linear + "_retireHandle(m_borrowHandle); "
               "m_borrowHandle = 0u; }\n";
        cpp += "        void (*release)(void*) = m_releaseUser;\n";
        cpp += "        m_releaseUser = nullptr;\n";
        cpp += "        if (release != nullptr) { release(m_user); }\n    }\n\n";
        cpp += "    void setBorrowHandle(uint64_t handle) { m_borrowHandle = handle; }\n";
        cpp += "    std::shared_ptr<cascaded_reverse::KeepAlive> keepAlive() const { return m_keepAlive; }\n\n";

        // 被绑定虚方法的代理体
        for (ReverseMethodPlan const& plan : plans)
        {
            MethodSignature const& method = *plan.method;
            std::vector<std::string> params;
            for (std::size_t index = 0; index < plan.params.size(); ++index)
            {
                params.push_back(bareType(method.params[index].first) + " " + plan.paramNames[index]);
            }
            const std::string paramText = joinWith(params, ", ");
            const std::string constSuffix = method.isConst ? " const" : "";
            if (plan.kind == 0)
            {
                cpp += "    void " + method.name + "(" + paramText + ")" + constSuffix + " override\n    {\n";
                cpp += "        reportPendingError(\"" + method.name + "\");\n";
                cpp += "        if (m_slots." + method.name + " != nullptr)\n        {\n";
                std::vector<std::string> args;
                for (std::size_t index = 0; index < plan.params.size(); ++index)
                {
                    args.push_back(reverseSpecIsBool(plan.params[index])
                                       ? ("(" + plan.paramNames[index] + " ? 1 : 0)")
                                       : plan.paramNames[index]);
                }
                cpp += "            m_slots." + method.name + "(m_user" +
                       (args.empty() ? std::string() : ", " + joinWith(args, ", ")) + ");\n";
                cpp += "        }\n    }\n\n";
                continue;
            }
            if (plan.kind == 1)
            {
                cpp += "    " + bareType(method.returnSpelling) + " " + method.name + "(" + paramText + ")" +
                       constSuffix + " override\n    {\n";
                cpp += "        reportPendingError(\"" + method.name + "\");\n";
                // 空槽守卫必须**按返回类型给值**：非 void 方法裸 `return ;` 是 C2561（实测过：
                // 夹具的 `int32_t Add` 守卫漏了返回值）。`return {};` 对标量 / castl::string /
                // 指针都成立（值初始化），void 根本走不到这里（kind 0 单独处理）。
                cpp += "        if (m_slots." + method.name + " == nullptr) { return {}; }\n";
                std::vector<std::string> args;
                for (std::size_t index = 0; index < plan.params.size(); ++index)
                {
                    args.push_back(reverseSpecIsBool(plan.params[index])
                                       ? ("(" + plan.paramNames[index] + " ? 1 : 0)")
                                       : plan.paramNames[index]);
                }
                const std::string call = "m_slots." + method.name + "(m_user" +
                                         (args.empty() ? std::string() : ", " + joinWith(args, ", ")) + ")";
                if (reverseSpecIsBool(plan.retScalar))
                {
                    cpp += "        return " + call + " != 0;\n";
                }
                else
                {
                    cpp += "        return " + call + ";\n";
                }
                cpp += "    }\n\n";
                continue;
            }
            if (plan.kind == 2)
            {
                cpp += "    " + bareType(method.returnSpelling) + " " + method.name + "(" + paramText + ")" +
                       constSuffix + " override\n    {\n";
                cpp += "        reportPendingError(\"" + method.name + "\");\n";
                cpp += "        const uint64_t needed = (m_slots." + method.name + "Len != nullptr) ? "
                       "m_slots." + method.name + "Len(m_user) : UINT64_MAX;\n";
                cpp += "        if (needed == UINT64_MAX) { setError(" + linear +
                       "_ERROR_REVERSE_EXCEPTION, takeImplementationError()); return "
                       + bareType(method.returnSpelling) + "(); }\n";
                cpp += "        " + bareType(method.returnSpelling) +
                       " text(static_cast<std::size_t>(needed), '\\0');\n";
                cpp += "        if (needed > 0u)\n        {\n";
                cpp += "            const uint64_t written = (m_slots.Copy" + method.name + " != nullptr) ? "
                       "m_slots.Copy" + method.name + "(m_user, text.data(), needed) : UINT64_MAX;\n";
                cpp += "            if (written == UINT64_MAX || written > needed) { setError(" + linear +
                       "_ERROR_REVERSE_EXCEPTION, takeImplementationError()); return "
                       + bareType(method.returnSpelling) + "(); }\n";
                cpp += "            text.resize(static_cast<std::size_t>(written));\n        }\n";
                cpp += "        return text;\n    }\n\n";
                continue;
            }
            if (plan.kind == 3)
            {
                // 写路径：与 TResource 同构（Write + SubmitAndWait），失败中止且不写。
                cpp += "    void " + method.name + "(ca_io::WBatch* inWriter)" + constSuffix + " override\n    {\n";
                cpp += "        reportPendingError(\"" + method.name + "\");\n";
                cpp += "        if (std::this_thread::get_id() != m_registrationThread)\n        {\n";
                cpp += "            setError(" + linear + "_ERROR_CALLBACK_WRONG_THREAD, "
                       "\"非注册线程调用反向接口 " + method.name + "（C# 覆写未执行）\");\n";
                cpp += "            return;\n        }\n";
                cpp += "        const uint64_t length = (m_slots.SerializeLen != nullptr) ? "
                       "m_slots.SerializeLen(m_user) : UINT64_MAX;\n";
                cpp += "        if (length == UINT64_MAX)\n        {\n";
                cpp += "            const std::string detail = takeImplementationError();\n";
                cpp += "            setError(" + linear + "_ERROR_REVERSE_EXCEPTION, detail.empty() ? "
                       "\"反向实现返回长度失败\" : detail);\n";
                cpp += "            return;\n        }\n";
                cpp += "        std::vector<unsigned char> buffer(static_cast<std::size_t>(length));\n";
                cpp += "        uint64_t written = 0u;\n";
                cpp += "        if (length > 0u)\n        {\n";
                cpp += "            written = (m_slots.SerializeCopy != nullptr) ? "
                       "m_slots.SerializeCopy(m_user, buffer.data(), length) : UINT64_MAX;\n";
                cpp += "            if (written == UINT64_MAX || written > length)\n            {\n";
                cpp += "                const std::string detail = takeImplementationError();\n";
                cpp += "                setError(" + linear + "_ERROR_REVERSE_EXCEPTION, detail.empty() ? "
                       "\"反向实现拷贝失败\" : detail);\n";
                cpp += "                return;\n            }\n";
                cpp += "        }\n";
                cpp += "        if (inWriter == nullptr) { setError(" + linear +
                       "_ERROR_NULL_POINTER, \"Serialize 收到空批\"); return; }\n";
                cpp += "        if (written > 0u) { inWriter->Write(written, buffer.data()); }\n";
                cpp += "        inWriter->SubmitAndWait();\n    }\n\n";
                continue;
            }
            // kind == 4：返回另一个反向接口的裸指针
            cpp += "    " + plan.objectTargetQualified + "* " + method.name + "(" + paramText + ")" +
                   constSuffix + " override\n    {\n";
            cpp += "        reportPendingError(\"" + method.name + "\");\n";
            cpp += "        if (m_slots." + method.name + " == nullptr) { return nullptr; }\n";
            std::vector<std::string> args;
            for (std::size_t index = 0; index < plan.params.size(); ++index)
            {
                args.push_back(reverseSpecIsBool(plan.params[index])
                                   ? ("(" + plan.paramNames[index] + " ? 1 : 0)")
                                   : plan.paramNames[index]);
            }
            cpp += "        void* created = m_slots." + method.name + "(m_user" +
                   (args.empty() ? std::string() : ", " + joinWith(args, ", ")) + ");\n";
            cpp += "        if (created == nullptr)\n        {\n";
            cpp += "            const std::string detail = takeImplementationError();\n";
            cpp += "            setError(" + linear + "_ERROR_REVERSE_EXCEPTION, detail.empty() ? "
                   "\"反向工厂 Create 返回了空指针\" : detail);\n";
            cpp += "            reportPendingError(\"" + method.name + "\");\n";
            cpp += "            return nullptr;\n        }\n";
            cpp += "        return static_cast<" + plan.objectTargetQualified + "*>(created);\n    }\n\n";
        }
        // CA_BIND_SKIP 跳过的纯虚方法：代理必须给出实现，语义是"本批不支持"。
        for (MethodSignature const* skipped : skippedPure)
        {
            std::vector<std::string> params;
            for (std::size_t index = 0; index < skipped->params.size(); ++index)
            {
                // **原样保留类型拼写**（含 `*` / `&`）：剥成 bareType 会让 override 签名与纯虚声明
                // 不一致 —— 代理仍是抽象类，`new Proxy` 直接编不过（实测过一次）。
                params.push_back(skipped->params[index].first + " /*跳过*/");
            }
            const bool returnsVoid = bareType(skipped->returnSpelling) == "void";
            cpp += "    " + skipped->returnSpelling + " " + skipped->name + "(" +
                   joinWith(params, ", ") + ")" + (skipped->isConst ? " const" : "") + " override\n    {\n";
            cpp += "        throw cascaded_reverse::ReverseBindingError(" + linear +
                   "_ERROR_REVERSE_READ_UNSUPPORTED, \"反向接口 " + shape.qualifiedName + "::" +
                   skipped->name + " 不支持（本批不绑这个方法）\");\n";
            if (!returnsVoid)
            {
                cpp += "        return " + bareType(skipped->returnSpelling) + "();\n";
            }
            cpp += "    }\n\n";
        }
        cpp += "private:\n";
        cpp += "    void setError(int32_t code, std::string const& text) const\n    {\n";
        cpp += "        std::lock_guard<std::mutex> guard(m_errorMutex);\n";
        cpp += "        m_errorCode = code;\n        m_errorText = text;\n    }\n";
        cpp += "    std::string takeImplementationError() const\n    {\n";
        cpp += "        if (m_slots.TakeLastError == nullptr) { return std::string(); }\n";
        cpp += "        const uint64_t needed = m_slots.TakeLastError(m_user, nullptr, 0u);\n";
        cpp += "        if (needed == 0u || needed == UINT64_MAX) { return std::string(); }\n";
        // 托管侧约定：cap 里留一个字节放 NUL（只拷 min(len, cap-1)）。所以这里必须申请 needed+1、
        // 传 needed+1 —— 只传 needed 会把最后一个字节截掉（实测：失败文本少一字节，
        // tester 里 `find(原始文本)` 会假红）。
        cpp += "        std::string text(static_cast<std::size_t>(needed) + 1u, '\\0');\n";
        cpp += "        const uint64_t written = m_slots.TakeLastError(m_user, text.data(), needed + 1u);\n";
        cpp += "        if (written > needed) { return std::string(); }\n";
        cpp += "        text.resize(static_cast<std::size_t>(written));\n        return text;\n    }\n";
        cpp += "    // 失败的可观测契约：错误先落槽，**下一次**注册线程调用把它抛出去（不静默继续）。\n";
        cpp += "    void reportPendingError(char const* methodName) const\n    {\n";
        cpp += "        int32_t code = 0;\n        std::string text;\n        {\n";
        cpp += "            std::lock_guard<std::mutex> guard(m_errorMutex);\n";
        cpp += "            code = m_errorCode;\n            text = m_errorText;\n";
        cpp += "            m_errorCode = 0;\n            m_errorText.clear();\n        }\n";
        cpp += "        if (code == 0) { return; }\n";
        cpp += "        std::string message(\"反向接口 " + shape.qualifiedName + "::\");\n";
        cpp += "        message += methodName;\n        message += \" 失败（错误码 \";\n";
        cpp += "        message += std::to_string(code);\n        message += \"）：\";\n";
        cpp += "        message += text;\n";
        cpp += "        throw cascaded_reverse::ReverseBindingError(code, message);\n    }\n";
        cpp += "    void* m_user = nullptr;\n";
        cpp += "    Slots m_slots{};\n";
        cpp += "    void (*m_releaseUser)(void*) = nullptr;\n";
        cpp += "    std::shared_ptr<cascaded_reverse::KeepAlive> m_keepAlive;\n";
        cpp += "    std::thread::id m_registrationThread{};\n";
        cpp += "    uint64_t m_borrowHandle = 0u;\n";
        cpp += "    mutable std::mutex m_errorMutex;\n";
        cpp += "    mutable int32_t m_errorCode = 0;\n";
        cpp += "    mutable std::string m_errorText;\n};\n\n";

        // ---- 反向函数表（登记表路由用；也是 C# 取 CreateImpl 的入口）----
        cpp += "typedef struct " + linear + "Api_v2 {\n";
        cpp += "    uint32_t abi_version;\n    uint32_t size;\n";
        cpp += "    uint64_t (*CreateImpl)(void* user, " + allSlotDeclText + ", void (*releaseUser)(void* user));\n";
        cpp += "    void* (*CreateProxy)(void* user, " + allSlotDeclText +
               ", void (*releaseUser)(void* user), uint64_t* outHandle);\n";
        cpp += "    int32_t (*ReleaseImpl)(uint64_t handle);\n";
        cpp += "    uint64_t (*ReverseKeepAliveCount)(uint64_t handle);\n";
        cpp += "    void* (*ResolveImpl)(uint64_t handle);\n";
        cpp += "} " + linear + "Api_v2;\n\n";
        cpp += "extern \"C\" uint64_t " + linear + "__CreateImpl(void* user, " + allSlotDeclText +
               ", void (*releaseUser)(void* user))\n{\n";
        cpp += "    " + linear + "Proxy* proxy = new " + linear + "Proxy(user, " +
               joinWith(allSlotNames(plans), ", ") + (allSlotNames(plans).empty() ? "" : ", ") +
               "TakeLastError, releaseUser);\n";
        cpp += "    std::shared_ptr<void> owner(static_cast<void*>(proxy), [](void* released) { delete static_cast<" +
               linear + "Proxy*>(released); });\n";
        cpp += "    const uint64_t handle = " + linear + "_registerStrongHandle(proxy, owner);\n";
        cpp += "    if (handle == 0u) { delete proxy; return 0u; }\n";
        cpp += "    " + linear + "_noteKeepAlive(handle, proxy->keepAlive());\n";
        cpp += "    proxy->setBorrowHandle(handle);\n";
        cpp += "    return handle;\n}\n\n";
        cpp += "extern \"C\" void* " + linear + "__CreateProxy(void* user, " + allSlotDeclText +
               ", void (*releaseUser)(void* user), uint64_t* outHandle)\n{\n";
        cpp += "    " + linear + "Proxy* proxy = new " + linear + "Proxy(user, " +
               joinWith(allSlotNames(plans), ", ") + (allSlotNames(plans).empty() ? "" : ", ") +
               "TakeLastError, releaseUser);\n";
        cpp += "    const uint64_t handle = " + linear + "_registerBorrowHandle(proxy);\n";
        cpp += "    if (handle == 0u) { delete proxy; if (outHandle != nullptr) { *outHandle = 0u; } "
               "return nullptr; }\n";
        cpp += "    " + linear + "_noteKeepAlive(handle, proxy->keepAlive());\n";
        cpp += "    proxy->setBorrowHandle(handle);\n";
        cpp += "    if (outHandle != nullptr) { *outHandle = handle; }\n";
        cpp += "    return static_cast<void*>(static_cast<" + shape.qualifiedName + "*>(proxy));\n}\n\n";
        cpp += "extern \"C\" int32_t " + linear + "__ReleaseImpl(uint64_t handle)\n{\n";
        cpp += "    return " + linear + "_retireHandle(handle);\n}\n\n";
        // 句柄 → 借用对象指针（**在创建它的模块里**解析）。句柄表是"生成在头里的 inline + 函数级
        // static"，EXE 与 DLL 各一份：把句柄交给别的模块的解析入口会查错表（实测：tester.exe 查
        // 自己的表拿不到 CSharpHost.dll 创建的句柄）。所以解析入口必须跟着句柄表走，并通过
        // 反向函数表（GetApi）暴露给跨模块调用方。
        cpp += "static void* " + linear + "_resolveHandle(uint64_t handle)\n{\n";
        cpp += "    std::lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex());\n";
        cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
        cpp += "    const uint64_t index = handle & indexMask;\n";
        cpp += "    const uint64_t generation = handle >> " + linear + "_HANDLE_INDEX_BITS;\n";
        cpp += "    if (index == 0u || index > " + linear + "_handles.size()) { return nullptr; }\n";
        cpp += "    cascaded_bindings::HandleEntry& entry = " + linear + "_handles[index - 1u];\n";
        cpp += "    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation || "
               "entry.interfaceTag != &" + linear + "_HANDLE_TAG) { return nullptr; }\n";
        cpp += "    return const_cast<void*>(entry.object);\n}\n";
        cpp += "extern \"C\" void* " + linear + "__ResolveImpl(uint64_t handle)\n{\n";
        cpp += "    return " + linear + "_resolveHandle(handle);\n}\n\n";
        // 单一所有权移交：C++ 调用方（注册表 / 测试）把强句柄换成 shared_ptr，表项降级为借用。
        // ⚠ 只对**同模块**调用方成立（shared_ptr 与句柄表都不跨模块）；跨模块用 `__ResolveImpl`
        // 拿借用指针 + `__ReleaseImpl` 释放强句柄。
        cpp += "std::shared_ptr<" + shape.qualifiedName + "> " + linear + "__TakeOwnership(uint64_t handle)\n{\n";
        cpp += "    return std::static_pointer_cast<" + shape.qualifiedName + ">(" + linear +
               "_takeStrongHandle(handle));\n}\n\n";
        cpp += "extern \"C\" uint32_t " + linear + "__GetApi(uint32_t version, uint32_t size, " +
               linear + "Api_v2* out)\n{\n";
        cpp += "    if (out == nullptr || version != 2u || size < static_cast<uint32_t>(sizeof(" + linear +
               "Api_v2))) { return 0u; }\n";
        cpp += "    " + linear + "Api_v2 table{};\n";
        cpp += "    table.abi_version = 2u;\n";
        cpp += "    table.size = static_cast<uint32_t>(sizeof(" + linear + "Api_v2));\n";
        cpp += "    table.CreateImpl = &" + linear + "__CreateImpl;\n";
        cpp += "    table.CreateProxy = &" + linear + "__CreateProxy;\n";
        cpp += "    table.ReleaseImpl = &" + linear + "__ReleaseImpl;\n";
        cpp += "    table.ReverseKeepAliveCount = &" + linear + "__ReverseKeepAliveCount;\n";
        cpp += "    table.ResolveImpl = &" + linear + "__ResolveImpl;\n";
        cpp += "    ::memcpy(out, &table, sizeof(table));\n";
        cpp += "    return static_cast<uint32_t>(sizeof(table));\n}\n\n";
        cpp += "extern \"C\" uint32_t " + linear + "__GetLayoutTypeCount(void)\n{\n    return 0u;\n}\n";
        cpp += "extern \"C\" uint32_t " + linear +
               "__GetLayoutType(uint32_t, char*, uint32_t, uint64_t*, uint32_t*)\n{\n    return 0u;\n}\n";
        cpp += "extern \"C\" uint32_t " + linear +
               "__GetLayoutField(uint32_t, uint32_t, char*, uint32_t, char*, uint32_t, uint64_t*)\n"
               "{\n    return 0u;\n}\n";

        // ---- 3. C# 产物 ----
        std::string cs;
        cs += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
        cs += "// CODEGEN-REVERSE-BINDING: " + shape.qualifiedName + "\n";
        cs += "using System;\n";
        cs += "using System.Runtime.CompilerServices;\n";
        cs += "using System.Runtime.InteropServices;\n";
        cs += "using System.Text;\n\n";
        cs += "namespace " + csNamespace + "\n{\n";
        cs += "    /// <summary>反向接口 " + shape.qualifiedName + " 的公开抽象基类：C# 侧实现它，\n";
        cs += "    /// 经生成的 CreateImpl / CreateProxy 注册给原生代理。**公开类型里只有托管方法**，\n";
        cs += "    /// 句柄 / 错误文本 / 字节缓存全部放在 internal 桥的 ConditionalWeakTable 里。</summary>\n";
        cs += "    public abstract class " + baseName + "\n    {\n";
        for (ReverseMethodPlan const& plan : plans)
        {
            MethodSignature const& method = *plan.method;
            std::vector<std::string> params;
            for (std::size_t index = 0; index < plan.params.size(); ++index)
            {
                params.push_back(std::string(plan.params[index]->csType) + " " + plan.paramNames[index]);
            }
            std::string csReturn;
            if (plan.kind == 0)
            {
                csReturn = "void";
            }
            else if (plan.kind == 1)
            {
                csReturn = plan.retScalar->csType;
            }
            else if (plan.kind == 2)
            {
                csReturn = "string";
            }
            else if (plan.kind == 3)
            {
                csReturn = "byte[]";
            }
            else
            {
                csReturn = reverseBaseName(plan.objectTargetShort);
            }
            cs += "        /// <summary>对应 C++ " + shape.qualifiedName + "::" + method.name + "</summary>\n";
            cs += "        public abstract " + csReturn + " " + method.name + "(" +
                  joinWith(params, ", ") + ");\n\n";
        }
        cs += "    }\n\n";
        cs += "    internal static unsafe class " + shape.name + "Bindings\n    {\n";
        cs += "        internal const int ERROR_STALE_HANDLE = 1001;\n";
        cs += "        internal const int ERROR_NULL_POINTER = 1002;\n";
        cs += "        internal const int ERROR_CALLBACK_WRONG_THREAD = 1005;\n";
        cs += "        internal const int ERROR_REVERSE_EXCEPTION = 1012;\n";
        cs += "        internal const int ERROR_REVERSE_READ_UNSUPPORTED = 1013;\n\n";
        // 每个反向实例的托管侧状态（错误文本 / 借用句柄 / 字节缓存）：**放在 internal 桥里**，
        // 公开抽象基类一个字段都不留 —— 否则 "公开类型里没有原生细节（含 ulong 句柄）" 那条检查
        // 会把 `internal ulong` 字段也判进去（它按类体逐行看，分不出字段的可见性）。
        cs += "        /// <summary>每实例的托管侧状态（句柄 / 错误文本 / 字节缓存）；公开基类里不存任何原生细节。</summary>\n";
        cs += "        private sealed class InstanceState\n        {\n";
        cs += "            public InstanceState() { }\n";
        cs += "            public string LastError = string.Empty;\n";
        cs += "            public ulong BorrowHandle;\n";
        cs += "            public byte[] Serialized;\n";
        cs += "            public byte[] StringBuffer;\n";
        cs += "        }\n\n";
        cs += "        private static readonly ConditionalWeakTable<" + baseName +
              ", InstanceState> States = new ConditionalWeakTable<" + baseName + ", InstanceState>();\n";
        cs += "        private static InstanceState StateOf(" + baseName + " target)\n        {\n";
        cs += "            if (target == null) { throw new ArgumentNullException(nameof(target)); }\n";
        cs += "            return States.GetOrCreateValue(target);\n        }\n\n";
        cs += "        /// <summary>C# 借用的那个句柄（只作归还时的令牌；注册表才是对象的强所有者）。</summary>\n";
        cs += "        internal static ulong GetBorrowHandle(" + baseName + " target)\n        {\n";
        cs += "            if (target == null) { return 0UL; }\n";
        cs += "            return StateOf(target).BorrowHandle;\n        }\n";
        cs += "        internal static void SetBorrowHandle(" + baseName + " target, ulong handle)\n        {\n";
        cs += "            if (target != null) { StateOf(target).BorrowHandle = handle; }\n        }\n";
        cs += "        private static void SetLastError(" + baseName + " target, string text)\n        {\n";
        cs += "            if (target != null) { StateOf(target).LastError = text; }\n        }\n\n";
        cs += "        // 反向函数表（第五类形状：一张函数表 + 一个不透明 user）。槽顺序就是 CreateImpl 的参数顺序。\n";
        cs += "        public static delegate* unmanaged<void*, " + allSlotCsText +
               ", delegate* unmanaged<void*, void>, ulong> CreateImplSlot;\n";
        cs += "        public static delegate* unmanaged<void*, " + allSlotCsText +
               ", delegate* unmanaged<void*, void>, ulong*, void*> CreateProxySlot;\n";
        cs += "        public static delegate* unmanaged<ulong, int> ReleaseImplSlot;\n";
        cs += "        public static delegate* unmanaged<ulong, ulong> KeepAliveCountSlot;\n";
        cs += "        public static delegate* unmanaged<ulong, void*> ResolveImplSlot;\n\n";
        cs += "        /// <summary>从 get_interface_api 取到的反向函数表里装槽（偏移就是函数表顺序）。</summary>\n";
        cs += "        public static void Bind(byte* table)\n        {\n";
        cs += "            if (table == null) { throw new ArgumentNullException(nameof(table)); }\n";
        cs += "            CreateImplSlot = *(delegate* unmanaged<void*, " + allSlotCsText +
               ", delegate* unmanaged<void*, void>, ulong>*)(table + 8);\n";
        cs += "            CreateProxySlot = *(delegate* unmanaged<void*, " + allSlotCsText +
               ", delegate* unmanaged<void*, void>, ulong*, void*>*)(table + 16);\n";
        cs += "            ReleaseImplSlot = *(delegate* unmanaged<ulong, int>*)(table + 24);\n";
        cs += "            KeepAliveCountSlot = *(delegate* unmanaged<ulong, ulong>*)(table + 32);\n";
        cs += "            ResolveImplSlot = *(delegate* unmanaged<ulong, void*>*)(table + 40);\n";
        cs += "        }\n\n";
        std::string csBridgeRefsText;
        {
            std::vector<std::string> refs;
            for (std::string const& slotName : allSlotNames(plans))
            {
                refs.push_back("&On" + slotName);
            }
            refs.push_back("&OnTakeLastError");
            refs.push_back("&OnReleaseUser");
            csBridgeRefsText = joinWith(refs, ", ");
        }
        cs += "        // ---- 注册入口（托管侧唯一的两条创建通道）----\n";
        cs += "        /// <summary>建强句柄（句柄表持强引用）。失败时原生路径自己调 releaseUser，</summary>\n";
        cs += "        /// <summary>这里 MUST NOT 再 Free 一次（唯一一次 Free 在托管桥里由原生回调触发）。</summary>\n";
        cs += "        internal static ulong Create(" + baseName + " target)\n        {\n";
        cs += "            if (target == null) { throw new ArgumentNullException(nameof(target)); }\n";
        cs += "            if (CreateImplSlot == null) { throw new InvalidOperationException(\"反向接口 " +
              shape.qualifiedName + " 的函数表还没绑定（先取表再调 Bind）\"); }\n";
        cs += "            GCHandle keepAlive = GCHandle.Alloc(target);\n";
        cs += "            ulong handle = CreateImplSlot((void*)GCHandle.ToIntPtr(keepAlive), " +
              csBridgeRefsText + ");\n";
        cs += "            if (handle == 0UL) { throw new InvalidOperationException(\"反向接口 " +
              shape.qualifiedName + "：CreateImpl 返回 0\"); }\n";
        cs += "            SetBorrowHandle(target, handle);\n            return handle;\n        }\n\n";
        cs += "        /// <summary>建借用句柄（不持强引用；单一所有者在调用方 shared_ptr / 注册表）。</summary>\n";
        cs += "        internal static IntPtr CreateProxy(" + baseName + " target, out ulong handle)\n        {\n";
        cs += "            if (target == null) { throw new ArgumentNullException(nameof(target)); }\n";
        cs += "            if (CreateProxySlot == null) { throw new InvalidOperationException(\"反向接口 " +
              shape.qualifiedName + " 的函数表还没绑定（先取表再调 Bind）\"); }\n";
        cs += "            GCHandle keepAlive = GCHandle.Alloc(target);\n";
        cs += "            ulong borrowHandle = 0UL;\n";
        cs += "            IntPtr native = (IntPtr)CreateProxySlot((void*)GCHandle.ToIntPtr(keepAlive), " +
              csBridgeRefsText + ", &borrowHandle);\n";
        cs += "            if (native == IntPtr.Zero) { throw new InvalidOperationException(\"反向接口 " +
              shape.qualifiedName + "：CreateProxy 返回空\"); }\n";
        cs += "            handle = borrowHandle;\n            SetBorrowHandle(target, borrowHandle);\n";
        cs += "            return native;\n        }\n\n";
        cs += "        /// <summary>释放 C# 借用的那份句柄（注册表仍持对象）。返回 0 成功，1001 = 已失效。</summary>\n";
        cs += "        internal static int Release(ulong handle)\n        {\n";
        cs += "            if (ReleaseImplSlot == null) { return ERROR_NULL_POINTER; }\n";
        cs += "            return ReleaseImplSlot(handle);\n        }\n\n";
        cs += "        internal static ulong KeepAliveCount(ulong handle)\n        {\n";
        cs += "            if (KeepAliveCountSlot == null) { return ulong.MaxValue; }\n";
        cs += "            return KeepAliveCountSlot(handle);\n        }\n\n";
        cs += "        /// <summary>句柄 → 借用对象指针（在本模块的表里解析；跨模块调用方用它，别自己查表）。</summary>\n";
        cs += "        internal static IntPtr Resolve(ulong handle)\n        {\n";
        cs += "            if (ResolveImplSlot == null) { return IntPtr.Zero; }\n";
        cs += "            return (IntPtr)ResolveImplSlot(handle);\n        }\n\n";
        cs += "        static " + baseName + " Target(void* user)\n        {\n";
        cs += "            return (" + baseName + ")GCHandle.FromIntPtr((IntPtr)user).Target;\n        }\n\n";
        cs += "        [UnmanagedCallersOnly]\n";
        cs += "        static void OnReleaseUser(void* user)\n        {\n";
        cs += "            GCHandle handle = GCHandle.FromIntPtr((IntPtr)user);\n";
        cs += "            if (handle.IsAllocated) { handle.Free(); }\n        }\n\n";
        cs += "        [UnmanagedCallersOnly]\n";
        cs += "        static ulong OnTakeLastError(void* user, byte* destination, ulong capacity)\n        {\n";
        cs += "            try\n            {\n";
        cs += "                " + baseName + " target = Target(user);\n";
        cs += "                byte[] bytes = Encoding.UTF8.GetBytes(StateOf(target).LastError ?? string.Empty);\n";
        cs += "                if (capacity == 0UL) { return (ulong)bytes.Length; }\n";
        cs += "                ulong room = capacity - 1UL;\n";
        cs += "                int take = (ulong)bytes.Length <= room ? bytes.Length : (int)room;\n";
        cs += "                if (take > 0) { new ReadOnlySpan<byte>(bytes, 0, take).CopyTo(new Span<byte>(destination, take)); }\n";
        cs += "                destination[take] = 0;\n";
        cs += "                StateOf(target).LastError = string.Empty;\n";
        cs += "                return (ulong)take;\n            }\n";
        cs += "            catch { return (ulong)0; }\n        }\n\n";
        // 每方法的托管桥
        for (ReverseMethodPlan const& plan : plans)
        {
            MethodSignature const& method = *plan.method;
            if (plan.kind == 0)
            {
                cs += "        [UnmanagedCallersOnly]\n";
                {
                    const std::vector<std::string> slotParams = reverseCsSlotParamDecls(plan);
                    cs += "        static void On" + method.name + "(void* user" +
                          (slotParams.empty() ? "" : ", " + joinWith(slotParams, ", ")) + ")\n        {\n";
                }
                cs += "            try\n            {\n";
                cs += "                " + baseName + " target = Target(user);\n";
                cs += "                target." + method.name + "(" +
                      joinWith(reverseCsCallArgs(plan), ", ") + ");\n            }\n";
                cs += "            catch (Exception ex)\n            {\n";
                cs += "                SetLastError(TargetSafe(user), ex.GetType().Name + \": \" + ex.Message);\n            }\n";
                cs += "        }\n\n";
                continue;
            }
            if (plan.kind == 1)
            {
                cs += "        [UnmanagedCallersOnly]\n";
                {
                    const std::vector<std::string> slotParams = reverseCsSlotParamDecls(plan);
                    cs += "        static " + std::string(plan.retScalar->csNative) + " On" + method.name +
                          "(void* user" + (slotParams.empty() ? "" : ", " + joinWith(slotParams, ", ")) +
                          ")\n        {\n";
                }
                cs += "            try\n            {\n";
                cs += "                " + baseName + " target = Target(user);\n";
                cs += "                " + std::string(plan.retScalar->csType) + " result = target." +
                      method.name + "(" + joinWith(reverseCsCallArgs(plan), ", ") + ");\n";
                if (std::string(plan.retScalar->canonical) == "bool")
                {
                    cs += "                return result ? 1 : 0;\n";
                }
                else
                {
                    cs += "                return result;\n";
                }
                cs += "            }\n            catch (Exception ex)\n            {\n";
                cs += "                SetLastError(TargetSafe(user), ex.GetType().Name + \": \" + ex.Message);\n";
                if (std::string(plan.retScalar->canonical) == "bool")
                {
                    cs += "                return 0;\n";
                }
                else
                {
                    cs += "                return default;\n";
                }
                cs += "            }\n        }\n\n";
                continue;
            }
            if (plan.kind == 2)
            {
                cs += "        [UnmanagedCallersOnly]\n";
                cs += "        static ulong On" + method.name + "Len(void* user)\n        {\n";
                cs += "            try\n            {\n";
                cs += "                " + baseName + " target = Target(user);\n";
                cs += "                string result = target." + method.name + "(" +
                      joinWith(reverseCsCallArgs(plan), ", ") + ");\n";
                cs += "                StateOf(target).StringBuffer = Encoding.UTF8.GetBytes(result ?? string.Empty);\n";
                cs += "                return (ulong)StateOf(target).StringBuffer.Length;\n            }\n";
                cs += "            catch (Exception ex)\n            {\n";
                cs += "                SetLastError(TargetSafe(user), ex.GetType().Name + \": \" + ex.Message);\n";
                cs += "                return ulong.MaxValue;\n            }\n        }\n\n";
                cs += "        [UnmanagedCallersOnly]\n";
                cs += "        static ulong OnCopy" + method.name + "(void* user, byte* destination, ulong capacity)\n        {\n";
                cs += "            try\n            {\n";
                cs += "                " + baseName + " target = Target(user);\n";
                cs += "                byte[] bytes = StateOf(target).StringBuffer ?? Array.Empty<byte>();\n";
                cs += "                ulong length = (ulong)bytes.Length;\n";
                cs += "                if (length == 0UL) { return 0UL; }\n";
                cs += "                if (destination == null || capacity < length)\n                {\n";
                cs += "                    StateOf(target).LastError = \"字符串返回值缓冲区不足\";\n";
                cs += "                    return ulong.MaxValue;\n                }\n";
                cs += "                fixed (byte* source = bytes) { Buffer.MemoryCopy(source, destination, (long)capacity, (long)length); }\n";
                cs += "                return length;\n            }\n            catch (Exception ex)\n            {\n";
                cs += "                SetLastError(TargetSafe(user), ex.GetType().Name + \": \" + ex.Message);\n";
                cs += "                return ulong.MaxValue;\n            }\n        }\n\n";
                continue;
            }
            if (plan.kind == 3)
            {
                cs += "        [UnmanagedCallersOnly]\n";
                cs += "        static ulong OnSerializeLen(void* user)\n        {\n";
                cs += "            try\n            {\n";
                cs += "                " + baseName + " target = Target(user);\n";
                cs += "                StateOf(target).Serialized = target." + method.name + "();\n";
                cs += "                return StateOf(target).Serialized == null ? 0UL : (ulong)StateOf(target).Serialized.Length;\n";
                cs += "            }\n            catch (Exception ex)\n            {\n";
                cs += "                SetLastError(TargetSafe(user), ex.GetType().Name + \": \" + ex.Message);\n";
                cs += "                return ulong.MaxValue;\n            }\n        }\n\n";
                cs += "        [UnmanagedCallersOnly]\n";
                cs += "        static ulong OnSerializeCopy(void* user, void* destination, ulong capacity)\n        {\n";
                cs += "            try\n            {\n";
                cs += "                " + baseName + " target = Target(user);\n";
                cs += "                byte[] bytes = StateOf(target).Serialized ?? Array.Empty<byte>();\n";
                cs += "                ulong length = (ulong)bytes.Length;\n";
                cs += "                if (length == 0UL) { return 0UL; }\n";
                cs += "                if (destination == null || capacity < length)\n                {\n";
                cs += "                    StateOf(target).LastError = \"Serialize 返回的字节缓冲区不足\";\n";
                cs += "                    return ulong.MaxValue;\n                }\n";
                cs += "                fixed (byte* source = bytes) { Buffer.MemoryCopy(source, destination, (long)capacity, (long)length); }\n";
                cs += "                return length;\n            }\n            catch (Exception ex)\n            {\n";
                cs += "                SetLastError(TargetSafe(user), ex.GetType().Name + \": \" + ex.Message);\n";
                cs += "                return ulong.MaxValue;\n            }\n        }\n\n";
                continue;
            }
            // kind == 4：返回另一个反向接口的抽象基类实例
            cs += "        [UnmanagedCallersOnly]\n";
            {
                const std::vector<std::string> slotParams = reverseCsSlotParamDecls(plan);
                cs += "        static void* On" + method.name + "(void* user" +
                      (slotParams.empty() ? "" : ", " + joinWith(slotParams, ", ")) + ")\n        {\n";
            }
            cs += "            try\n            {\n";
            cs += "                " + baseName + " target = Target(user);\n";
            cs += "                " + reverseBaseName(plan.objectTargetShort) + " result = target." +
                  method.name + "(" + joinWith(reverseCsCallArgs(plan), ", ") + ");\n";
            cs += "                if (result == null)\n                {\n";
            cs += "                    StateOf(target).LastError = \"工厂 Create 返回了 null（MUST NOT 注册空资源）\";\n";
            cs += "                    return null;\n                }\n";
            cs += "                ulong resourceHandle = 0UL;\n";
            cs += "                IntPtr native = global::" + cSharpFullNameOf(plan.objectTargetQualified) +
                  "Bindings.CreateProxy(result, out resourceHandle);\n";
            cs += "                global::" + cSharpFullNameOf(plan.objectTargetQualified) +
                  "Bindings.SetBorrowHandle(result, resourceHandle);\n";
            cs += "                return (void*)native;\n";
            cs += "            }\n            catch (Exception ex)\n            {\n";
            cs += "                SetLastError(TargetSafe(user), ex.GetType().Name + \": \" + ex.Message);\n";
            cs += "                return null;\n            }\n        }\n\n";
        }
        // 失败时给"实现对象"写错误文本：user 解不出来时静默（C++ 侧会按空文本处理）。
        cs += "        static " + baseName + " TargetSafe(void* user)\n        {\n";
        cs += "            try { return Target(user); } catch { return null; }\n        }\n";
        cs += "    }\n}\n";

        // ---- 4. 写文件 ----
        const std::string cppPath = outDir + "/" + linear + ".Bindings.cpp";
        const std::string csPath = outDir + "/" + linear + ".Bindings.cs";
        std::ofstream cppOut(cppPath, std::ios::binary);
        cppOut << cpp;
        if (!cppOut)
        {
            std::fprintf(stderr, "CodeGen: cannot write '%s'\n", cppPath.c_str());
            return 2;
        }
        std::ofstream csOut(csPath, std::ios::binary);
        csOut << cs;
        if (!csOut)
        {
            std::fprintf(stderr, "CodeGen: cannot write '%s'\n", csPath.c_str());
            return 2;
        }
        std::fprintf(stderr, "  [反向] %s：代理 + 抽象基类 %s + glue（被绑定虚方法 %zu 条）\n",
                     shape.qualifiedName.c_str(), baseName.c_str(), plans.size());
        return 0;
    }

    int run(const char* filePath, const char* outDir, const std::vector<const char*>& clangArgs,
            const std::string& auditScriptPath, unsigned handleIndexBits, unsigned handleGenerationBits,
            std::string const& registryPartLabel, std::string const& registryPartDir,
            std::string const& idLockPath, std::string const& idScope)
    {
        const std::string sourceText = readWholeFile(filePath);
        const std::vector<BoundMarker> markers = scanBoundMarkers(sourceText);
        // `CA_BIND_VALUE` 的标注可以写在被包含的头里（句柄值类在它们自己的定义文件里标注，
        // 例如 `ShaderResourceHandle.h` 的 ImageHandle / BufferHandle）：只扫主头会把它们漏掉，
        // 于是 `GPUGraph::ScheduleData(ImageHandle const&, …)` 这类方法的参数认不出值类。
        // 解析之后再补扫整个包含闭包（下面的 unit 建好以后）。
        std::set<std::string> valueTypeNames = scanValueTypeNames(sourceText);
        std::vector<std::string> bound;          // 正向接口（CA_BIND）
        std::vector<std::string> reverseBound;   // 反向接口（CA_BIND_REVERSE）
        bound.reserve(markers.size());
        for (BoundMarker const& marker : markers)
        {
            (marker.reverse ? reverseBound : bound).push_back(marker.interfaceName);
        }
        if (bound.empty() && reverseBound.empty())
        {
            std::fprintf(stderr, "CodeGen: --emit-binding found no CA_BIND marker in '%s'.\n", filePath);
            return 1;
        }

        // 带号标注一律拒绝（本 change 任务 1.4）：编号的唯一来源是锁文件，标注只声明绑定意图。
        // 放在解析之前 —— 这是纯文本判定，不需要 AST，也保证被拒的那一轮一个产物都不写。
        {
            int numberedMarkers = 0;
            for (BoundMarker const& marker : markers)
            {
                if (marker.hasNumber)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝 %s —— 编号由锁文件治理、标注无参数"
                                 "（标注里写的是 \"%s\"；编号 %u 无效）\n",
                                 marker.interfaceName.c_str(), marker.numberText.c_str(), marker.number);
                    ++numberedMarkers;
                }
            }
            if (numberedMarkers > 0)
            {
                return 1;
            }
        }

        // 登记表输入（本 change 任务 1.2）：编号从锁文件读，标注只声明绑定意图。
        // 锁的路径与段由调用方（build.py）传入；缺锁时**任何**登记输入都不许生成 —— 否则
        // "编号唯一来源是锁"就退化成口头约定。
        if (!registryPartLabel.empty() && (idLockPath.empty() || idScope.empty()))
        {
            std::fprintf(stderr,
                         "CodeGen: 拒绝登记 %s —— 登记表输入必须给 --id-lock <锁文件> 与 --id-scope <host|fixture>\n",
                         filePath != nullptr ? filePath : "");
            return 1;
        }

        // ① 模板/泛型 与"排除没写理由"只看源码文本就够了，必须在解析**之前**判 ——
        // 模板夹具本身就不是合法声明（宏展开成 static_assert 会插在 template 与 class 之间），
        // 先解析只会得到一个与类别无关的 parse error。
        {
            int earlyRejections = 0;
            for (BoundMarker const& marker : markers)
            {
                if (marker.isTemplate)
                {
                    std::fprintf(stderr, "CodeGen: 拒绝 %s —— ① 模板/泛型：绑定对象不能是模板类\n",
                                 marker.interfaceName.c_str());
                    ++earlyRejections;
                }
                for (auto const& skip : marker.skipReasons)
                {
                    if (skip.second.empty())
                    {
                        std::fprintf(stderr,
                                     "CodeGen: 拒绝 %s::%s —— 排除必须写理由（CA_BIND_SKIP(方法, \"理由\")）\n",
                                     marker.interfaceName.c_str(), skip.first.c_str());
                        ++earlyRejections;
                    }
                }
            }
            if (earlyRejections > 0)
            {
                return 1;
            }
        }
        std::vector<const char*> args;
        args.push_back("-std=c++20");
        args.push_back("-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH");
        for (const char* argument : clangArgs)
        {
            args.push_back(argument);
        }

        CXIndex index = clang_createIndex(1, 0);
        CXTranslationUnit unit = nullptr;
        const CXErrorCode error = clang_parseTranslationUnit2(
            index, filePath, args.data(), static_cast<int>(args.size()), nullptr, 0,
            CXTranslationUnit_DetailedPreprocessingRecord, &unit);
        if (error != CXError_Success || unit == nullptr)
        {
            std::fprintf(stderr, "CodeGen: libclang parse failed (error=%d) for '%s'\n",
                         static_cast<int>(error), filePath);
            clang_disposeIndex(index);
            return 3;
        }

        // 诊断逐条打出来便于定位；但只有**错误及以上**才拒绝继续 —— 警告（例如 `#pragma once in main
        // file`）在绑定模式下是正常的，按警告拒会让批量跑不通。
        const unsigned diagnosticCount = clang_getNumDiagnostics(unit);
        unsigned errorCount = 0;
        for (unsigned i = 0; i < diagnosticCount; ++i)
        {
            CXDiagnostic diagnostic = clang_getDiagnostic(unit, i);
            CXString text = clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions());
            std::fprintf(stderr, "  libclang: %s\n", clang_getCString(text));
            clang_disposeString(text);
            if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error)
            {
                ++errorCount;
            }
            clang_disposeDiagnostic(diagnostic);
        }
        if (errorCount > 0)
        {
            clang_disposeTranslationUnit(unit);
            clang_disposeIndex(index);
            std::fprintf(stderr, "CodeGen: 绑定解析有 %u 条错误诊断，拒绝继续 —— 先把头里的错误修掉。\n",
                         errorCount);
            return 3;
        }

        std::vector<InterfaceShape> shapes;
        ClassCollector collector;
        collector.wanted = &bound;
        collector.sink = &shapes;
        clang_visitChildren(clang_getTranslationUnitCursor(unit), collectClasses, &collector);
        // 反向接口（csharp-reverse-interface-binding）：单独收一份形状，**不进**正向 `shapes` ——
        // 正向那一路的投影/拒绝/句柄/回调判据对反向产物都不适用（反向走独立发射分支）。
        std::vector<InterfaceShape> reverseShapes;
        if (!reverseBound.empty())
        {
            ClassCollector reverseCollector;
            reverseCollector.wanted = &reverseBound;
            reverseCollector.sink = &reverseShapes;
            clang_visitChildren(clang_getTranslationUnitCursor(unit), collectClasses, &reverseCollector);
            for (InterfaceShape& shape : reverseShapes)
            {
                shape.isReverse = true;
            }
            // 反向接口必须真的找到类定义（标注写了但没有类 = 生成期拒绝）。
            int missingReverse = 0;
            for (std::string const& wanted : reverseBound)
            {
                bool foundAny = false;
                for (InterfaceShape const& shape : reverseShapes)
                {
                    if (shape.name == wanted)
                    {
                        foundAny = true;
                        break;
                    }
                }
                if (!foundAny)
                {
                    std::fprintf(stderr, "CodeGen: 拒绝 %s —— CA_BIND_REVERSE 标了它，但找不到类定义\n",
                                 wanted.c_str());
                    ++missingReverse;
                }
            }
            if (missingReverse > 0)
            {
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
            for (InterfaceShape const& shape : reverseShapes)
            {
                g_bindableInterfaceNames.insert(shape.name);
                bool alreadyKnown = false;
                for (auto const& entry : g_knownInterfaces)
                {
                    if (entry.first == shape.name && entry.second == shape.qualifiedName)
                    {
                        alreadyKnown = true;
                        break;
                    }
                }
                if (!alreadyKnown)
                {
                    g_knownInterfaces.emplace_back(shape.name, shape.qualifiedName);
                }
            }
        }
        // `CA_BIND_VALUE` 补扫包含闭包（见上面 valueTypeNames 的说明），同时记下"包含闭包里出现过
        // CA_BIND_REVERSE 的接口名"（IResourceFactory::Create() 返回 IResource*，而 IResource 的标注
        // 在它 include 的头里 —— 反查目标接口要靠这份清单）。必须在 dispose 之前收。
        std::set<std::string> reverseKnownNames;
        for (InterfaceShape const& shape : reverseShapes)
        {
            reverseKnownNames.insert(shape.name);
        }
        {
            std::vector<std::string> includedFiles;
            InclusionCollector inclusionCollector;
            inclusionCollector.paths = &includedFiles;
            clang_getInclusions(unit, collectInclusion, &inclusionCollector);
            for (std::string const& path : includedFiles)
            {
                if (path.empty())
                {
                    continue;
                }
                const std::string includedText = readWholeFile(path.c_str());
                const std::set<std::string> extra = scanValueTypeNames(includedText);
                valueTypeNames.insert(extra.begin(), extra.end());
                for (BoundMarker const& included : scanBoundMarkers(includedText))
                {
                    if (included.reverse)
                    {
                        reverseKnownNames.insert(included.interfaceName);
                    }
                }
            }
        }
        // 接口名清单：`projectMethod` 用它判"返回的是不是另一个可绑定接口的裸指针"（借用句柄，
        // 任务 5.3 / design D9）。主头里的接口 + `--known-interface` 传来的其它生成输入（跨头目标）
        // 两边都按**完整限定名**登记。本批 1.1：同一个短名可以有多条完整限定名（跨命名空间同名）。
        g_bindableInterfaceNames.clear();
        for (InterfaceShape const& shape : shapes)
        {
            g_bindableInterfaceNames.insert(shape.name);
            bool alreadyKnown = false;
            for (auto const& entry : g_knownInterfaces)
            {
                if (entry.first == shape.name && entry.second == shape.qualifiedName)
                {
                    alreadyKnown = true;
                    break;
                }
            }
            if (!alreadyKnown)
            {
                g_knownInterfaces.emplace_back(shape.name, shape.qualifiedName);
            }
        }
        for (auto const& entry : g_knownInterfaces)
        {
            g_bindableInterfaceNames.insert(entry.first);
        }

        // 映射后 C# 全名碰撞（design D1）：这是本批唯一的同名拒绝点，判定在命名空间映射之后。
        // 两个不同的 C++ 类型映射到同一 C# 全名（命名空间路径 + 类名）⇒ 非零退出并点名双方。
        {
            std::map<std::string, std::string> mappedToQualified;   // C# 全名 → 完整限定名
            int collisions = 0;
            for (auto const& entry : g_knownInterfaces)
            {
                const std::string mapped = cSharpFullNameOf(entry.second);
                auto inserted = mappedToQualified.emplace(mapped, entry.second);
                if (!inserted.second && inserted.first->second != entry.second)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝 —— 映射后 C# 全名冲突 %s：%s / %s\n",
                                 mapped.c_str(), inserted.first->second.c_str(), entry.second.c_str());
                    ++collisions;
                }
            }
            if (collisions > 0)
            {
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
        }

        // ---- 资源引用值类（任务 3.1）：从 AST 提取 kind 枚举与构造通道 ----
        g_valueTypes.clear();
        g_serializedTypes.clear();
        g_serializedElementRecords.clear();
        if (!valueTypeNames.empty())
        {
            std::map<std::string, CXCursor> valueCursors;
            ValueCursorCollector valueCollector;
            valueCollector.wanted = &valueTypeNames;
            valueCollector.sink = &valueCursors;
            clang_visitChildren(clang_getTranslationUnitCursor(unit), collectValueClassCursor, &valueCollector);
            int valueProblems = 0;
            for (std::string const& name : valueTypeNames)
            {
                auto found = valueCursors.find(name);
                if (found == valueCursors.end())
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝 %s —— CA_BIND_VALUE 标注了它，但找不到类定义\n", name.c_str());
                    ++valueProblems;
                    continue;
                }
                ValueTypeInfo info;
                std::string whyNot;
                std::string classBody;
                if (!findClassBodyTextNearCursor(found->second, name, sourceText, &classBody))
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝 %s —— CA_BIND_VALUE 标注了它，但源码里找不到类体\n",
                                 name.c_str());
                    ++valueProblems;
                    continue;
                }
                if (!buildValueTypeInfo(found->second, classBody, &info, &whyNot))
                {
                    std::fprintf(stderr, "CodeGen: 拒绝 %s —— %s\n", name.c_str(), whyNot.c_str());
                    ++valueProblems;
                    continue;
                }
                g_valueTypes[name] = info;
            }
            if (valueProblems > 0)
            {
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
        }

        // ---- 编号锁核对（本 change 任务 1.2 / 1.4）----
        // 进登记表的接口一律从锁取号。带号标注已在上面无条件拒绝；这里只核对
        // "标注声明的名字在锁里有行、完整限定名一致、锁里属于本头的行都有标注"。
        std::map<std::string, unsigned> registryNumbers;   // 短名 → 锁里的编号
        if (!registryPartLabel.empty())
        {
            InterfaceLock lock;
            std::string lockError;
            if (!loadInterfaceLock(idLockPath, &lock, &lockError))
            {
                std::fprintf(stderr, "CodeGen: 拒绝登记 —— %s\n", lockError.c_str());
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
            LockScope* scope = findLockScope(lock, idScope);
            if (scope == nullptr)
            {
                std::string names;
                for (LockScope const& candidate : lock.scopes)
                {
                    names += (names.empty() ? "" : "、") + candidate.name;
                }
                std::fprintf(stderr,
                             "CodeGen: 拒绝登记 —— 锁文件里没有段 `%s`（现有段：%s）\n",
                             idScope.c_str(), names.c_str());
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
            int lockProblems = 0;
            std::map<std::string, std::string> qualifiedByMarker;
            for (InterfaceShape const& shape : shapes)
            {
                qualifiedByMarker[shape.name] = shape.qualifiedName;
            }
            for (BoundMarker const& marker : markers)
            {
                LockEntry const* entry = findLockEntry(*scope, marker.interfaceName,
                                                       qualifiedByMarker[marker.interfaceName]);
                if (entry == nullptr)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 %s —— 锁文件（段 %s）里没有它的行。新接口先跑 "
                                 "CodeGen --update-ids 发号，编号不许手填\n",
                                 marker.interfaceName.c_str(), idScope.c_str());
                    ++lockProblems;
                    continue;
                }
                // 完整限定名必须与头里声明的类一致（同名不同命名空间不许混进同一个号）。
                auto shape = std::find_if(shapes.begin(), shapes.end(),
                                          [&marker](InterfaceShape const& candidate) {
                                              return candidate.name == marker.interfaceName;
                                          });
                if (shape != shapes.end() &&
                    normalizeLockSlashes(shape->qualifiedName) !=
                        normalizeLockSlashes(entry->qualifiedName))
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 %s —— 锁里的完整限定名是 %s，头里声明的是 %s\n",
                                 marker.interfaceName.c_str(), entry->qualifiedName.c_str(),
                                 shape->qualifiedName.c_str());
                    ++lockProblems;
                    continue;
                }
                // 带号标注已在 run() 入口无条件拒绝（任务 1.4），这里只按锁取号。
                registryNumbers[marker.interfaceName] = entry->number;
            }
            // 锁里属于本头、但本头已经没有该标注的行：多一行也点名（删标注忘了删锁行时不会静默）。
            for (LockEntry const& entry : scope->entries)
            {
                if (normalizeLockSlashes(entry.header) !=
                    normalizeLockSlashes(filePath != nullptr ? filePath : ""))
                {
                    continue;
                }
                bool annotated = false;
                for (BoundMarker const& marker : markers)
                {
                    if (marker.interfaceName == entry.shortName)
                    {
                        annotated = true;
                        break;
                    }
                }
                if (!annotated)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 —— 锁文件里有 %s（编号 %u，头 %s），但该头没有对应的 "
                                 "CA_BIND 标注；删名字请跑 CodeGen --update-ids\n",
                                 entry.shortName.c_str(), entry.number, entry.header.c_str());
                    ++lockProblems;
                }
            }
            if (lockProblems > 0)
            {
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
        }

        // 排除方法（CA_BIND_SKIP 带理由）在各道上必须一致生效：先给方法打标记，后面的
        // 镜像收集 / 投影 / 发射统一看这一个标记。不这么做时"排除了还去收它的镜像" ——
        // 实测 CRenderBackend 的 Readback（std::span）与 ExecuteGraphTimed 仍被当成要建镜像的
        // 参数，报 `CRenderBackend::std::span<uint8_t>：找不到类型定义`。
        {
            std::map<std::string, std::set<std::string>> skippedNames;
            for (BoundMarker const& marker : markers)
            {
                for (auto const& skip : marker.skipReasons)
                {
                    if (!skip.second.empty())
                    {
                        skippedNames[marker.interfaceName].insert(skip.first);
                    }
                }
            }
            for (InterfaceShape& shape : shapes)
            {
                for (MethodSignature& method : shape.methods)
                {
                    if (skippedNames[shape.name].count(method.name) > 0)
                    {
                        method.skipped = true;
                    }
                }
            }
        }

        // ---- ① 模板成员函数：显式排除（CA_BIND_SKIP 带理由）或生成期拒绝（任务 2.2）----
        // 与"模板类"分开：模板类在任何情况下都拒绝；模板**成员函数**允许用 CA_BIND_SKIP 显式排除
        // （本批不做模板单态化，排除必须留理由 —— 现有的"排除必须写理由"检查仍然生效）。
        {
            std::map<std::string, std::set<std::string>> explicitSkips;
            std::map<std::string, BoundMarker const*> markerByName;
            for (BoundMarker const& marker : markers)
            {
                markerByName[marker.interfaceName] = &marker;
                for (auto const& skip : marker.skipReasons)
                {
                    if (!skip.second.empty())
                    {
                        explicitSkips[marker.interfaceName].insert(skip.first);
                    }
                }
            }
            int templateProblems = 0;
            for (InterfaceShape const& shape : shapes)
            {
                for (MethodSignature const& method : shape.methods)
                {
                    if (!method.isTemplate)
                    {
                        continue;
                    }
                    // 本批 D9：写了 `CA_BIND_INSTANTIATE` 的模板形态是要绑的（由展开通道处理），
                    // 不算"没被排除的模板"。
                    bool declaredInstantiation = false;
                    auto marker = markerByName.find(shape.name);
                    if (marker != markerByName.end())
                    {
                        declaredInstantiation =
                            marker->second->instantiateTypes.count(method.name) > 0;
                    }
                    if (declaredInstantiation)
                    {
                        continue;
                    }
                    // 同名已有可绑的非模板实现（真实形状：`GPUGraph::ScheduleData` 的两条内存窗
                    // 重载 + 两条模板便利形态）：模板形态自动排除并打日志，**不需要** CA_BIND_SKIP ——
                    // 按名字写 SKIP 会把同名非模板重载一起排掉（实测丢掉了 D2 的字节上传通道）。
                    bool shadowedByConcrete = false;
                    for (MethodSignature const& sibling : shape.methods)
                    {
                        if (!sibling.isTemplate && sibling.name == method.name &&
                            explicitSkips[shape.name].count(sibling.name) == 0)
                        {
                            shadowedByConcrete = true;
                            break;
                        }
                    }
                    if (shadowedByConcrete)
                    {
                        std::fprintf(stderr,
                                     "[排除] %s::%s（模板形态）—— 同名已有可绑的非模板实现\n",
                                     shape.name.c_str(), method.name.c_str());
                        continue;
                    }
                    if (explicitSkips[shape.name].count(method.name) == 0)
                    {
                        std::fprintf(stderr,
                                     "CodeGen: 拒绝 %s::%s —— ① 模板/泛型：成员函数模板不能绑定"
                                     "（本批不做模板单态化；要放过就用 CA_BIND_SKIP 显式排除并写理由）\n",
                                     shape.name.c_str(), method.name.c_str());
                        ++templateProblems;
                    }
                }
            }
            if (templateProblems > 0)
            {
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
        }

        // ---- 参数注入（任务 3.3）：裸指针参数 + 源码里写了固定名钩子声明 ----
        // 钩子命名：`CAInject__<限定名线形>__<方法名>__<参数名>()`，返回 `void*`。三条生成期拒绝：
        // 方法不存在 / 参数名不存在、参数不是裸指针、有注入声明但没有钩子声明。
        {
            std::map<std::string, BoundMarker const*> markerByName;
            for (BoundMarker const& marker : markers)
            {
                markerByName[marker.interfaceName] = &marker;
            }
            const std::string strippedSource = stripSourceComments(sourceText);
            int injectProblems = 0;
            for (InterfaceShape& shape : shapes)
            {
                auto marker = markerByName.find(shape.name);
                if (marker == markerByName.end() || marker->second->injectParams.empty())
                {
                    continue;
                }
                for (auto const& injection : marker->second->injectParams)
                {
                    const std::string& methodName = injection.first;
                    MethodSignature* target = nullptr;
                    int sameNameCount = 0;
                    for (MethodSignature& method : shape.methods)
                    {
                        if (method.name == methodName)
                        {
                            ++sameNameCount;
                            target = &method;
                        }
                    }
                    if (target == nullptr)
                    {
                        std::fprintf(stderr,
                                     "CodeGen: 拒绝 %s 的注入声明 —— 找不到方法 %s\n",
                                     shape.name.c_str(), methodName.c_str());
                        ++injectProblems;
                        continue;
                    }
                    if (sameNameCount > 1)
                    {
                        std::fprintf(stderr,
                                     "CodeGen: 拒绝 %s::%s 的注入声明 —— 同名重载方法不支持注入\n",
                                     shape.name.c_str(), methodName.c_str());
                        ++injectProblems;
                        continue;
                    }
                    for (std::string const& parameterName : injection.second)
                    {
                        int parameterIndex = -1;
                        for (std::size_t index = 0; index < target->params.size(); ++index)
                        {
                            if (target->params[index].second == parameterName)
                            {
                                parameterIndex = static_cast<int>(index);
                                break;
                            }
                        }
                        if (parameterIndex < 0)
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s 的注入声明 —— 参数名 %s 不存在\n",
                                         shape.name.c_str(), methodName.c_str(),
                                         parameterName.c_str());
                            ++injectProblems;
                            continue;
                        }
                        const std::string spelling =
                            trimCopy(target->params[static_cast<std::size_t>(parameterIndex)].first);
                        if (spelling.find('*') == std::string::npos)
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s 的注入声明 —— 参数 %s 不是裸指针"
                                         "（注入只能用于参数位置的裸指针）\n",
                                         shape.name.c_str(), methodName.c_str(),
                                         parameterName.c_str());
                            ++injectProblems;
                            continue;
                        }
                        const std::string hookName =
                            injectHookName(shape.qualifiedName, methodName, parameterName);
                        if (strippedSource.find(hookName + "(") == std::string::npos)
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s 的注入声明 —— 有注入声明但没有钩子声明"
                                         "（需要 extern \"C\" void* %s();）\n",
                                         shape.name.c_str(), methodName.c_str(), hookName.c_str());
                            ++injectProblems;
                            continue;
                        }
                        target->injectedParams.insert(parameterName);
                        target->ownerName = shape.name;
                        target->ownerQualified = shape.qualifiedName;
                    }
                }
            }
            if (injectProblems > 0)
            {
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
        }

        // ---- 自返回识别 + C ABI 槽名（任务 2.2 / 2.4 的过渡口径）----
        for (InterfaceShape& shape : shapes)
        {
            std::map<std::string, int> nameCounts;
            for (MethodSignature& method : shape.methods)
            {
                if (method.isTemplate)
                {
                    // 模板成员本身**不出槽**（D9）：它的具体形态由 `CA_BIND_INSTANTIATE` 的展开
                    // 克隆体承担。这里留空 abiName，后面所有投影 / 发射循环都会跳过它 ——
                    // 否则模板参数 `T` 会被当成真实类型去建镜像（实测报 "找不到类型定义 T"）。
                    method.abiName.clear();
                    continue;
                }
                const std::string bare = bareType(method.returnSpelling);
                method.returnsSelfRef = method.returnSpelling.find('&') != std::string::npos &&
                                        !bare.empty() && unqualifiedName(bare) == shape.name;
                const int ordinal = ++nameCounts[method.name];
                // 任务 2.4：第 1 槽保持现名，第 2 槽起 `_<序数>`（既有产物逐字节锚不受影响）。
                method.abiName = ordinal == 1 ? method.name
                                              : method.name + "_" + std::to_string(ordinal);
            }
        }

        // ---- 内存窗配对（csharp-native-direct-binding 任务 1.1 / design D2）----
        // 绑定方法里的 `const void*` 必须配到**相邻唯一**的整型长度参数；配对失败在投影之前
        // 非零点名（长度缺失 / 多个整型候选各一句），不许把裸指针暴露给托管、也不许猜。
        // 排除方法（CA_BIND_SKIP 带理由）不参与配对 —— 它们本来就不出绑定。
        {
            std::map<std::string, std::set<std::string>> skippedForPairing;
            for (BoundMarker const& marker : markers)
            {
                for (auto const& skip : marker.skipReasons)
                {
                    if (!skip.second.empty())
                    {
                        skippedForPairing[marker.interfaceName].insert(skip.first);
                    }
                }
            }
            int windowProblems = 0;
            for (InterfaceShape& shape : shapes)
            {
                for (MethodSignature& method : shape.methods)
                {
                    if (method.isTemplate)
                    {
                        continue;
                    }
                    if (skippedForPairing[shape.name].count(method.name) > 0)
                    {
                        continue;
                    }
                    for (std::size_t paramIndex = 0; paramIndex < method.params.size(); ++paramIndex)
                    {
                        if (method.injectedParams.count(method.params[paramIndex].second) > 0)
                        {
                            continue;
                        }
                        if (!isConstVoidPointerSpelling(method.params[paramIndex].first))
                        {
                            continue;
                        }
                        std::size_t lengthIndex = 0;
                        const int analysis = analyzeWindowPair(method, paramIndex, &lengthIndex);
                        if (analysis == 0)
                        {
                            method.windowLengthByPointer[paramIndex] = lengthIndex;
                            method.windowPointerByLength[lengthIndex] = paramIndex;
                            continue;
                        }
                        const std::string parameterName = method.params[paramIndex].second;
                        if (analysis == 1)
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s —— 内存窗配对失败：const void* 参数 %s "
                                         "没有相邻整型长度参数（相邻形参都不是整型），不猜\n",
                                         shape.name.c_str(), method.name.c_str(), parameterName.c_str());
                        }
                        else
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s —— 内存窗配对不唯一：const void* 参数 %s "
                                         "两侧有多个整型候选（%s / %s），不猜\n",
                                         shape.name.c_str(), method.name.c_str(), parameterName.c_str(),
                                         method.params[paramIndex - 1].second.c_str(),
                                         method.params[paramIndex + 1].second.c_str());
                        }
                        ++windowProblems;
                    }
                }
            }
            if (windowProblems > 0)
            {
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
        }

        // ---- 枚举元数据（任务 2.2）：同短名不同限定名必须拒绝，不能静默覆盖 ----
        g_enumsByShort.clear();
        {
            std::vector<EnumInfo> enums;
            EnumWalkState enumWalk;
            enumWalk.sink = &enums;
            clang_visitChildren(clang_getTranslationUnitCursor(unit), collectEnumsVisitor, &enumWalk);
            int enumConflicts = 0;
            for (EnumInfo const& info : enums)
            {
                auto existing = g_enumsByShort.find(info.shortName);
                if (existing != g_enumsByShort.end() && existing->second.cppName != info.cppName)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝 —— 同短名枚举 %s 有两种完整限定名：%s / %s\n",
                                 info.shortName.c_str(), existing->second.cppName.c_str(),
                                 info.cppName.c_str());
                    ++enumConflicts;
                    continue;
                }
                g_enumsByShort[info.shortName] = info;
            }
            if (enumConflicts > 0)
            {
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
        }

        // ---- 模板显式实例化声明（本批 D9）：把声明的形态展开成具体方法 ----
        // 放在枚举元数据之后：类型名可解析性检查要能认出枚举 / 记录 / 资源引用值类。
        // 每个形态克隆一份 MethodSignature（模板参数名按标识符边界替换成具体类型），
        // C ABI 槽名 = `<方法名>_<类型记号>`，托管侧仍是同名重载。
        {
            std::map<std::string, CXCursor> instantiationRecords;
            {
                StructCursorCollector collector;
                collector.sink = &instantiationRecords;
                clang_visitChildren(clang_getTranslationUnitCursor(unit), collectStructCursorsForMirror,
                                    &collector);
            }
            std::map<std::string, BoundMarker const*> markerByName;
            for (BoundMarker const& marker : markers)
            {
                markerByName[marker.interfaceName] = &marker;
            }
            auto typeToken = [](std::string const& name) {
                std::string token;
                for (char character : name)
                {
                    token.push_back(std::isalnum(static_cast<unsigned char>(character)) ? character : '_');
                }
                return token;
            };
            int instantiateProblems = 0;
            for (InterfaceShape& shape : shapes)
            {
                auto marker = markerByName.find(shape.name);
                if (marker == markerByName.end() || marker->second->instantiateTypes.empty())
                {
                    continue;
                }
                std::vector<MethodSignature> expansions;
                for (MethodSignature const& method : shape.methods)
                {
                    if (!method.isTemplate)
                    {
                        continue;
                    }
                    auto declared = marker->second->instantiateTypes.find(method.name);
                    if (declared == marker->second->instantiateTypes.end())
                    {
                        continue;
                    }
                    for (std::string const& typeName : declared->second)
                    {
                        const std::string typeBare = bareType(typeName);
                        const bool knownType =
                            isScalarType(typeBare) ||
                            g_enumsByShort.count(unqualifiedName(typeBare)) > 0 ||
                            g_valueTypes.count(unqualifiedName(typeBare)) > 0 ||
                            instantiationRecords.count(unqualifiedName(typeBare)) > 0;
                        if (!knownType)
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s 的实例化声明 —— 类型 %s 在翻译单元里找不到"
                                         "（本批只接受标量 / 枚举 / 记录 / 资源引用值类）\n",
                                         shape.name.c_str(), method.name.c_str(), typeName.c_str());
                            ++instantiateProblems;
                            continue;
                        }
                        if (method.templateParams.empty())
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s 的实例化声明 —— 该方法不是带类型参数的模板\n",
                                         shape.name.c_str(), method.name.c_str());
                            ++instantiateProblems;
                            continue;
                        }
                        auto substitute = [&method, &typeBare](std::string text) {
                            for (std::string const& templateParam : method.templateParams)
                            {
                                std::string out;
                                for (std::size_t index = 0; index < text.size();)
                                {
                                    const bool boundaryBefore =
                                        index == 0 ||
                                        !(std::isalnum(static_cast<unsigned char>(text[index - 1])) ||
                                          text[index - 1] == '_');
                                    if (boundaryBefore &&
                                        text.compare(index, templateParam.size(), templateParam) == 0)
                                    {
                                        const std::size_t end = index + templateParam.size();
                                        const bool boundaryAfter =
                                            end >= text.size() ||
                                            !(std::isalnum(static_cast<unsigned char>(text[end])) ||
                                              text[end] == '_');
                                        if (boundaryAfter)
                                        {
                                            out += typeBare;
                                            index = end;
                                            continue;
                                        }
                                    }
                                    out.push_back(text[index]);
                                    ++index;
                                }
                                text = out;
                            }
                            return text;
                        };
                        MethodSignature clone = method;
                        clone.isTemplate = false;
                        clone.isPure = false;
                        clone.returnSpelling = substitute(method.returnSpelling);
                        clone.returnCanonicalSpelling = substitute(method.returnCanonicalSpelling);
                        for (auto& parameter : clone.params)
                        {
                            parameter.first = substitute(parameter.first);
                        }
                        for (std::string& canonical : clone.paramCanonicalSpellings)
                        {
                            canonical = substitute(canonical);
                        }
                        clone.abiName = method.name + "_" + typeToken(typeName);
                        clone.ownerName = shape.name;
                        clone.ownerQualified = shape.qualifiedName;
                        // 自返回识别在这一步之后不再重跑（槽名已定），克隆体自己算一次：
                        // `ShaderStruct::SetValue` 返回 `ShaderStruct&`，托管侧要能继续链。
                        {
                            const std::string cloneReturnBare = bareType(clone.returnSpelling);
                            clone.returnsSelfRef =
                                clone.returnSpelling.find('&') != std::string::npos &&
                                !cloneReturnBare.empty() &&
                                unqualifiedName(cloneReturnBare) == shape.name;
                        }
                        clone.templateParams.clear();
                        clone.instantiateTypes.clear();
                        expansions.push_back(std::move(clone));
                    }
                }
                if (!expansions.empty())
                {
                    shape.methods.insert(shape.methods.end(), expansions.begin(), expansions.end());
                }
            }
            if (instantiateProblems > 0)
            {
                clang_disposeTranslationUnit(unit);
                clang_disposeIndex(index);
                return 1;
            }
        }

        // ---- 审计表核对（任务 2.7）：子进程调 check_bindable_audit.py --json ----
        // CodeGen 是 C++、审计脚本是 Python，跨语言没有 import；这里只认它的 JSON 输出。
        {
            std::string scriptPath = auditScriptPath;
            if (scriptPath.empty())
            {
                std::string base = filePath != nullptr ? std::string(filePath) : std::string();
                const std::size_t slash = base.find_last_of("/\\");
                scriptPath = (slash == std::string::npos ? std::string(".") : base.substr(0, slash)) +
                             "/check_bindable_audit.py";
            }
            // `2>&1`：脚本自己会点名"缺少审计行：…"，把它一起收进来，非零退出时才有话可说
            const std::string command = "python \"" + scriptPath + "\" --json 2>&1";
            std::string auditOutput;
            int auditExit = -1;
            FILE* pipe = _popen(command.c_str(), "r");
            if (pipe != nullptr)
            {
                char buffer[4096];
                while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
                {
                    auditOutput += buffer;
                }
                auditExit = _pclose(pipe);
            }
            if (auditOutput.empty() || auditOutput.find('{') == std::string::npos)
            {
                std::fprintf(stderr,
                             "CodeGen: 找不到 python 或审计脚本没有产出 JSON（命令：%s）—— 绑定生成前必须核对审计表\n"
                             "  shell/脚本原话：%s\n",
                             command.c_str(), auditOutput.c_str());
                return 1;
            }
            // 解析 `"接口::方法"`（methods 字段里全是这种串）
            std::set<std::string> auditedMethods;
            std::set<std::string> auditedInterfaces;
            for (std::size_t i = 0; i < auditOutput.size();)
            {
                const std::size_t open = auditOutput.find('"', i);
                if (open == std::string::npos)
                {
                    break;
                }
                const std::size_t close = auditOutput.find('"', open + 1);
                if (close == std::string::npos)
                {
                    break;
                }
                const std::string token = auditOutput.substr(open + 1, close - open - 1);
                if (token.find("::") != std::string::npos)
                {
                    auditedMethods.insert(token);
                }
                else if (!token.empty() && token != "interfaces" && token != "methods")
                {
                    auditedInterfaces.insert(token);
                }
                i = close + 1;
            }
            if (auditedMethods.empty())
            {
                std::fprintf(stderr, "CodeGen: 审计脚本的 --json 输出里没有方法清单（退出码 %d）\n", auditExit);
                return 1;
            }
            int missingAudit = 0;
            for (const std::string& name : bound)
            {
                if (auditedInterfaces.find(name) == auditedInterfaces.end())
                {
                    std::fprintf(stderr, "CodeGen: 跳过审计核对 %s —— 它不在审计表覆盖的接口清单里\n", name.c_str());
                }
            }
            for (InterfaceShape const& shape : shapes)
            {
                for (MethodSignature const& method : shape.methods)
                {
                    // 2.2 过渡：同名第二槽不绑，不参与审计覆盖
                    if (method.abiName.empty())
                    {
                        continue;
                    }
                    // 审计表只覆盖真实接口；夹具接口不在表里时**明说跳过**，不是静默放行
                    if (auditedInterfaces.find(shape.name) == auditedInterfaces.end())
                    {
                        continue;
                    }
                    const std::string key = shape.name + "::" + method.name;
                    if (auditedMethods.find(key) == auditedMethods.end())
                    {
                        std::fprintf(stderr, "CodeGen: 拒绝 %s —— 缺少审计结论（审计表里没有这一行）\n",
                                     key.c_str());
                        ++missingAudit;
                    }
                }
            }
            if (auditExit != 0)
            {
                // 把脚本自己的话打出来（它会点名哪个方法缺审计行 / 哪一行是对不上的）
                std::fprintf(stderr, "CodeGen: 拒绝 —— 审计表对账不绿（脚本退出码 %d）：\n%s\n", auditExit,
                             auditOutput.c_str());
                return 1;
            }
            if (missingAudit > 0)
            {
                return 1;
            }
        }
        // 布局清单按**接口**分别收集：只发本接口投影里用到的记录类型（否则每个产物都会带上
        // 别的接口的镜像，C# 侧还会出现重复的公开记录类型）。
        std::map<std::string, std::vector<MirrorType>> mirrorsByInterface;
        std::vector<std::string> mirrorErrors;
        // 结构体游标按短名索引：镜像收集与 `HashObj<T>` 的哈希来源检查都要用。
        std::map<std::string, CXCursor> structCursors;
        {
            StructCursorCollector cursorCollector;
            cursorCollector.sink = &structCursors;
            clang_visitChildren(clang_getTranslationUnitCursor(unit), collectStructCursorsForMirror,
                                &cursorCollector);
            // ---- 记录两段式（本批 D8）：可投影的返回位置记录集合 ----
            // 必须在任何 projectMethod 调用之前定下来：`FindShaderStructData` 这类元数据查询
            // 只有先认出"指向可序列化记录的裸指针"才会走两段式，否则按 ② 拒绝。
            // 判定口径与序列化块同源（recordNeedsSerialization：成员含容器/文本 ⇒ 需要块路径）。
            g_recordReturnTypes.clear();
            for (InterfaceShape const& shape : shapes)
            {
                for (MethodSignature const& method : shape.methods)
                {
                    if (method.isTemplate)
                    {
                        continue;
                    }
                    if (method.returnSpelling.find('*') == std::string::npos)
                    {
                        continue;
                    }
                    const std::string returnBare = bareType(method.returnSpelling);
                    if (returnBare.empty() || returnBare.back() == '*')
                    {
                        continue;
                    }
                    auto record = structCursors.find(unqualifiedName(returnBare));
                    if (record == structCursors.end() || !recordNeedsSerialization(record->second))
                    {
                        continue;
                    }
                    g_recordReturnTypes.insert(unqualifiedName(returnBare));
                }
            }
            // 静态工厂的返回记录必须是纯标量记录（任务 2.3）：成员里出现容器 / 文本 / 指针 /
            // 智能指针就按"返回位置容器"生成期拒绝并点名，MUST NOT 把被禁形状标成可绑。
            {
                int staticProblems = 0;
                for (InterfaceShape const& shape : shapes)
                {
                    for (MethodSignature const& method : shape.methods)
                    {
                        if (!method.isStatic)
                        {
                            continue;
                        }
                        const std::string retBare = bareType(method.returnSpelling);
                        auto record = structCursors.find(unqualifiedName(retBare));
                        if (record == structCursors.end())
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s —— 静态工厂只支持返回纯标量记录"
                                         "（找不到 %s 的定义）\n",
                                         shape.name.c_str(), method.name.c_str(), retBare.c_str());
                            ++staticProblems;
                            continue;
                        }
                        struct NonScalarProbe
                        {
                            bool found = false;
                            std::string member;
                        } probe;
                        clang_visitChildren(
                            record->second,
                            [](CXCursor member, CXCursor, CXClientData data) {
                                if (clang_getCursorKind(member) != CXCursor_FieldDecl)
                                {
                                    return CXChildVisit_Continue;
                                }
                                CXType type = clang_getCanonicalType(clang_getCursorType(member));
                                const std::string spelling = toStd(clang_getTypeSpelling(type));
                                const bool nonScalar =
                                    isContainerType(type) || looksLikeManagedPointer(type) ||
                                    type.kind == CXType_Pointer ||
                                    spelling.find("NameHash") != std::string::npos ||
                                    spelling.find("PathHash") != std::string::npos;
                                if (!nonScalar)
                                {
                                    return CXChildVisit_Continue;
                                }
                                auto* state = static_cast<NonScalarProbe*>(data);
                                state->found = true;
                                state->member = toStd(clang_getCursorSpelling(member));
                                return CXChildVisit_Break;
                            },
                            &probe);
                        if (probe.found)
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s —— 返回位置容器：静态工厂不得返回含容器/文本/"
                                         "句柄的记录（%s::%s 是非标量成员；托管替代是镜像类型字段赋值；"
                                         "真实实例出现时立扩形状批）\n",
                                         shape.name.c_str(), method.name.c_str(), retBare.c_str(),
                                         probe.member.c_str());
                            ++staticProblems;
                        }
                    }
                }
                if (staticProblems > 0)
                {
                    clang_disposeTranslationUnit(unit);
                    clang_disposeIndex(index);
                    return 1;
                }
            }
            // ---- 序列化块记录（任务 3.2）：参数位置、成员含容器/文本的记录 ----
            // 它们不走镜像快速路径；本轮先从"被绑定方法用到的记录"里挑出非标量成员的那些，
            // 再逐字段建布局清单与块头哈希。超界形状在这里生成期拒绝（不留"能生成、调用失败"的中间态）。
            {
                std::set<std::string> candidates;
                std::set<std::string> returnedRecords;
                for (InterfaceShape const& shape : shapes)
                {
                    for (MethodSignature const& method : shape.methods)
                    {
                        // 只在**真会绑定**的方法上收返回类型：被 CA_BIND_SKIP 排除的方法（例如
                        // `GetRenderPassForWrite` 返回 `RenderPass&`）不该把 RenderPass 从
                        // 序列化候选里踢出去 —— 那会让 `AddPass(RenderPass const&)` 退到镜像路径，
                        // 而 RenderPass 的成员是私有的（实测报"m_PipelineStates 不是公开字段"）。
                        if (method.skipped || method.abiName.empty())
                        {
                            continue;
                        }
                        returnedRecords.insert(unqualifiedName(bareType(method.returnSpelling)));
                    }
                }
                for (InterfaceShape const& shape : shapes)
                {
                    for (MethodSignature const& method : shape.methods)
                    {
                        for (auto const& parameter : method.params)
                        {
                            const std::string bare = bareType(parameter.first);
                            if (bare.empty() || g_valueTypes.count(bare) > 0 || isScalarType(bare) ||
                                g_bindableInterfaceNames.count(unqualifiedName(bare)) > 0)
                            {
                                continue;
                            }
                            if (returnedRecords.count(unqualifiedName(bare)) > 0)
                            {
                                // 同时出现在返回位置的记录留给镜像路径：返回方向的容器/文本本来
                                // 就不在本批支持范围内，既有"嵌入记录"等拒绝诊断由那条路给出。
                                continue;
                            }
                            if (hasPrefix(bare, "castl::vector") || hasPrefix(bare, "std::vector") ||
                                hasPrefix(bare, "castl::string") || hasPrefix(bare, "std::string") ||
                                hasPrefix(bare, "castl::shared_ptr") || hasPrefix(bare, "std::shared_ptr") ||
                                hasPrefix(bare, "castl::weak_ptr") || hasPrefix(bare, "std::weak_ptr") ||
                                bare.find('*') != std::string::npos || isNameHashSpelling(bare) ||
                                isPathHashSpelling(bare))
                            {
                                continue;
                            }
                            auto record = structCursors.find(unqualifiedName(bare));
                            if (record == structCursors.end() ||
                                !recordNeedsSerialization(record->second))
                            {
                                continue;
                            }
                            candidates.insert(unqualifiedName(bare));
                        }
                    }
                }
                // 返回位置的可序列化记录（D8）也要建块：`FindShaderStructData` 系的两段式
                // 要靠同一个编解码器把记录搬给托管。
                candidates.insert(g_recordReturnTypes.begin(), g_recordReturnTypes.end());
                int serializedProblems = 0;
                // 逐条注册记录本体 + 它引出的嵌套记录 / 容器元素记录：三者都要单独的托管镜像
                // （嵌套记录是 `m_StructUniforms` 这类字段，元素记录是 vector 的元素类型）。
                auto registerSerializedRecord = [&structCursors, &sourceText](std::string const& rootName,
                                                                              std::string* error) -> bool {
                    std::vector<std::string> pending{ rootName };
                    std::set<std::string> visited;
                    while (!pending.empty())
                    {
                        const std::string name = pending.back();
                        pending.pop_back();
                        if (!visited.insert(name).second || g_serializedTypes.count(name) > 0)
                        {
                            continue;
                        }
                        auto record = structCursors.find(name);
                        if (record == structCursors.end())
                        {
                            *error = "找不到记录 " + name + " 的定义";
                            return false;
                        }
                        std::string classBody;
                        if (!findClassBodyTextNearCursor(record->second, name, sourceText, &classBody))
                        {
                            *error = "源码里找不到类体";
                            return false;
                        }
                        SerializedTypeInfo info;
                        std::string buildError;
                        if (!buildSerializedTypeInfo(record->second, classBody, &info, &buildError))
                        {
                            *error = buildError;
                            return false;
                        }
                        // 把记录里出现的嵌套记录 / 元素记录排进队列（先收完再落表，避免迭代器失效）。
                        std::vector<SerializedField const*> queue;
                        for (SerializedField const& field : info.fields)
                        {
                            queue.push_back(&field);
                        }
                        while (!queue.empty())
                        {
                            SerializedField const* field = queue.back();
                            queue.pop_back();
                            if (field->kind == 4 || field->kind == 7)
                            {
                                pending.push_back(field->nestedRecordShort);
                            }
                            if (field->kind == 8 && field->elementKind == 2)
                            {
                                pending.push_back(field->elementRecordShort);
                            }
                            // 映射的记录值（含 HashObj 包装的内层记录）也要单独出托管镜像：
                            // 托管字典的值类型就是它（实测缺 VertexInputsDescriptor 时报 CS0246）。
                            if (field->kind == 5 && field->mapValueKind == 2)
                            {
                                pending.push_back(field->mapValueRecordShort);
                            }
                            if (field->kind == 3 && field->elementKind == 2)
                            {
                                SerializedElementInfo element;
                                element.shortName = field->elementRecordShort;
                                element.qualifiedName = field->elementRecordQualified;
                                element.fields = field->elementFields;
                                g_serializedElementRecords[element.shortName] = element;
                            }
                            for (SerializedField const& sub : field->nestedFields)
                            {
                                queue.push_back(&sub);
                            }
                            for (SerializedField const& sub : field->elementFields)
                            {
                                queue.push_back(&sub);
                            }
                        }
                        // 返回位置的记录（D8 两段式）不许含"需要绑定对象表"的字段：D11 的
                        // MUST NOT 明确——返回位置的映射容器与对象引用维持不可绑，不静默丢字段。
                        if (g_recordReturnTypes.count(name) > 0)
                        {
                            for (SerializedField const& field : info.fields)
                            {
                                if (serializedFieldNeedsBindingTable(field))
                                {
                                    *error = "记录 " + name + " 是返回位置（记录两段式）的载体，但字段 " +
                                             field.name +
                                             " 是值类容器 / 命名映射 / 接口对象引用 —— design D11 的 "
                                             "MUST NOT：返回位置的这些形状维持不可绑（不静默丢字段）";
                                    return false;
                                }
                            }
                        }
                        g_serializedTypes[name] = std::move(info);
                    }
                    return true;
                };
                for (std::string const& name : candidates)
                {
                    std::string serializedError;
                    if (!registerSerializedRecord(name, &serializedError))
                    {
                        std::fprintf(stderr, "CodeGen: 拒绝序列化块记录 %s —— %s\n", name.c_str(),
                                     serializedError.c_str());
                        ++serializedProblems;
                    }
                }
                if (serializedProblems > 0)
                {
                    clang_disposeTranslationUnit(unit);
                    clang_disposeIndex(index);
                    return 1;
                }
            }
            for (InterfaceShape const& shape : shapes)
            {
                std::set<std::string> wantedMirrors;
                // 诊断：记录"这个镜像名是哪条方法/哪个投影类型带来的"（镜像构建失败时能点到方法）。
                std::map<std::string, std::string> mirrorSources;
                auto noteType = [&wantedMirrors, &mirrorSources](std::string const& projectedType,
                                                                std::string const& source) {
                    std::string bare = projectedType;
                    while (!bare.empty() && (bare.back() == '*' || bare.back() == ' '))
                    {
                        bare.pop_back();
                    }
                    if (bare.size() > 6 && bare.compare(bare.size() - 6, 6, " const") == 0)
                    {
                        bare = bare.substr(0, bare.size() - 6);
                    }
                    if (bare.size() > 1 && bare.back() == 'C' && !isScalarType(bare))
                    {
                        const std::string name = bare.substr(0, bare.size() - 1);
                        wantedMirrors.insert(name);
                        mirrorSources.emplace(name, source);
                    }
                };
                for (MethodSignature const& method : shape.methods)
                {
                    if (method.abiName.empty() || method.skipped)
                    {
                        continue;   // 2.2 过渡：同名第二槽不绑；排除方法不参与镜像收集
                    }
                    std::vector<Projected> params;
                    std::string returnType;
                    std::string rejectReason;
                    bool needsTwoStep = false;
                    bool isHandleReturn = false;
                    if (!projectMethod(method, &params, &returnType, &rejectReason, &needsTwoStep, &isHandleReturn))
                    {
                        continue;
                    }
                    noteType(returnType, method.name + " 返回");
                    for (Projected const& projected : params)
                    {
                        noteType(projected.type, method.name + " 参数 " + projected.name);
                    }
                }
                MirrorBuildCtx ctx;
                ctx.sink = &mirrorsByInterface[shape.name];
                ctx.errors = &mirrorErrors;
                ctx.interfaceName = shape.name;
                for (std::string const& wanted : wantedMirrors)
                {
                    auto found = structCursors.find(wanted);
                    if (found == structCursors.end())
                    {
                        mirrorErrors.push_back(shape.name + "::" + wanted + "：找不到类型定义");
                        continue;
                    }
                    buildMirror(ctx, clang_getCursorType(found->second), qualifiedCursorName(found->second),
                                wanted);
                    std::fprintf(stderr, "[镜像] %s ← %s\n", wanted.c_str(),
                                 mirrorSources[wanted].c_str());
                }
            }
        }
        if (!mirrorErrors.empty())
        {
            // 非法嵌套在本 change 前是"只跳过布局清单、退出码 0"；现在收紧为**非零退出并点名路径**。
            for (std::string const& error : mirrorErrors)
            {
                std::fprintf(stderr, "CodeGen: 拒绝嵌套记录 —— %s\n", error.c_str());
            }
            return 1;
        }

        // ---- `HashObj<T>` 的显式哈希来源（任务 2.4 / spec「哈希对象只传所含值」）----
        // T 除满足记录条件外，还必须有消费翻译单元可见的来源：类型声明前的 `CA_REFLECT`、
        // 同文件里的 `CA_HASH_BY_BYTES(T)` 或 `custom_hash_trait<T>` 特化。没有就**在生成期**
        // 拒绝，不能留到 C++ 的静态断言才暴露。
        {
            auto hasExplicitHashSource = [&structCursors, filePath](std::string const& shortName) -> bool {
                auto found = structCursors.find(shortName);
                if (found == structCursors.end())
                {
                    return false;
                }
                CXSourceLocation location = clang_getCursorLocation(found->second);
                CXFile file = nullptr;
                unsigned offset = 0;
                clang_getSpellingLocation(location, &file, nullptr, nullptr, &offset);
                const std::string path = file != nullptr ? toStd(clang_getFileName(file)) : std::string();
                const std::string text = path.empty() ? std::string() : readWholeFile(path.c_str());
                const std::string mainText = filePath != nullptr ? readWholeFile(filePath) : std::string();
                auto mentionsSource = [&shortName](std::string const& source) {
                    return source.find("CA_HASH_BY_BYTES(" + shortName) != std::string::npos ||
                           source.find("CA_HASH_BY_BYTES( " + shortName) != std::string::npos ||
                           source.find("custom_hash_trait<" + shortName) != std::string::npos ||
                           source.find("custom_hash_trait< " + shortName) != std::string::npos;
                };
                if (mentionsSource(text) || mentionsSource(mainText))
                {
                    return true;
                }
                if (!text.empty())
                {
                    const std::size_t begin = offset > 400u ? offset - 400u : 0u;
                    const std::string before =
                        text.substr(begin, offset > begin ? offset - begin : 0u);
                    if (before.find("CA_REFLECT(") != std::string::npos)
                    {
                        return true;
                    }
                }
                return false;
            };
            int hashProblems = 0;
            for (InterfaceShape const& shape : shapes)
            {
                const std::vector<MirrorType>& mirrors = mirrorsByInterface[shape.name];
                auto findMirror = [&mirrors](std::string const& shortName) -> MirrorType const* {
                    for (MirrorType const& mirror : mirrors)
                    {
                        if (unqualifiedName(mirror.cppName) == shortName)
                        {
                            return &mirror;
                        }
                    }
                    return nullptr;
                };
                for (MethodSignature const& method : shape.methods)
                {
                    if (method.abiName.empty())
                    {
                        continue;   // 2.2 过渡：同名第二槽不绑
                    }
                    for (auto const& parameter : method.params)
                    {
                        std::string hashInner;
                        if (!parseHashObjWrapper(parameter.first, &hashInner))
                        {
                            continue;
                        }
                        const std::string innerShort = unqualifiedName(bareType(hashInner));
                        if (findMirror(innerShort) == nullptr)
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s —— cacore::HashObj<T> 的 T（%s）不满足公开"
                                         "数据记录条件（公开字段 / 标准布局 / 可默认构造 / 无虚函数）\n",
                                         shape.name.c_str(), method.name.c_str(), innerShort.c_str());
                            ++hashProblems;
                            continue;
                        }
                        if (!hasExplicitHashSource(innerShort))
                        {
                            std::fprintf(stderr,
                                         "CodeGen: 拒绝 %s::%s —— cacore::HashObj<T> 的 T（%s）缺少消费翻译"
                                         "单元可见的显式哈希来源（CA_REFLECT / custom_hash_trait / "
                                         "CA_HASH_BY_BYTES）\n",
                                         shape.name.c_str(), method.name.c_str(), innerShort.c_str());
                            ++hashProblems;
                        }
                    }
                }
            }
            if (hashProblems > 0)
            {
                return 1;
            }
        }

        // 任务 2.8：把"有纯虚方法但没写 CA_BIND"的接口列出来（不生成、不报错，但必须可见）
        {
            std::vector<std::string> unbound;
            UnboundCollector unboundCollector;
            // 反向接口也算"已声明绑定意图"：它们走 CA_BIND_REVERSE，不能在这里被报成"没写标注"。
            std::vector<std::string> allBoundNames = bound;
            allBoundNames.insert(allBoundNames.end(), reverseBound.begin(), reverseBound.end());
            unboundCollector.bound = &allBoundNames;
            unboundCollector.sink = &unbound;
            unboundCollector.mainFile = [&] {
                std::string name = filePath != nullptr ? std::string(filePath) : std::string();
                const std::size_t slash = name.find_last_of("/\\");
                if (slash != std::string::npos) { name = name.substr(slash + 1); }
                for (char& character : name) { character = static_cast<char>(std::tolower(static_cast<unsigned char>(character))); }
                return name;
            }();
            clang_visitChildren(clang_getTranslationUnitCursor(unit), collectUnboundInterfaces,
                                &unboundCollector);
            for (const std::string& name : unbound)
            {
                std::fprintf(stderr, "CodeGen: 跳过 %s —— 没有 CA_BIND 标注（不生成绑定）\n", name.c_str());
            }
        }

        // 诊断：逐接口打印收集到的方法名（重复收集会在这里一眼看出来）
        for (InterfaceShape const& shape : shapes)
        {
            std::string methodNames;
            for (MethodSignature const& method : shape.methods)
            {
                methodNames += method.name + " ";
            }
            std::fprintf(stderr, "  [接口] %s → %zu 条：%s\n", shape.name.c_str(), shape.methods.size(),
                         methodNames.c_str());
        }

        clang_disposeTranslationUnit(unit);
        clang_disposeIndex(index);

        int rejected = 0;
        int methodsTotal = 0;
        int products = 0;
        // 本批 1.1：逐接口 / 共享类型产出的 .cs 路径（写完统一补跨命名空间 using）。
        std::vector<std::string> writtenCsPaths;
        // ---- 拒绝口径（任务 2.6）：只有 ① 模板与 ③ 隐式转换重载自动判；② ④ 必须带理由排除 ----
        std::map<std::string, BoundMarker const*> markerByName;
        for (BoundMarker const& marker : markers)
        {
            markerByName[marker.interfaceName] = &marker;
        }
        std::map<std::string, std::set<std::string>> skippedMethods;
        for (auto const& entry : markerByName)
        {
            BoundMarker const& marker = *entry.second;
            // 反向接口走独立分支（自己的 skip 理由在反向发射里逐条打印）：正向的拒绝口径
            // （模板 / 排除没写理由）对它不适用，别把它的排除理由混进正向日志。
            if (marker.reverse)
            {
                continue;
            }
            if (marker.isTemplate)
            {
                std::fprintf(stderr, "CodeGen: 拒绝 %s —— ① 模板/泛型：绑定对象不能是模板类\n",
                             marker.interfaceName.c_str());
                ++rejected;
            }
            for (auto const& skip : marker.skipReasons)
            {
                if (skip.second.empty())
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝 %s::%s —— 排除必须写理由（CA_BIND_SKIP(方法, \"理由\")）\n",
                                 marker.interfaceName.c_str(), skip.first.c_str());
                    ++rejected;
                }
                else
                {
                    std::fprintf(stderr, "CodeGen: 排除 %s::%s —— %s\n", marker.interfaceName.c_str(),
                                 skip.first.c_str(), skip.second.c_str());
                    skippedMethods[marker.interfaceName].insert(skip.first);
                }
            }
        }
        // ③：同一接口里两条方法投影成**同一个 C ABI 签名**（典型是 `int32_t` 与 `bool` 的重载）
        for (InterfaceShape const& shape : shapes)
        {
            std::map<std::string, std::string> seenSignatures;
            for (MethodSignature const& method : shape.methods)
            {
                if (method.abiName.empty())
                {
                    continue;   // 2.2 过渡：同名第二槽不绑
                }
                // 带理由排除的方法：不出绑定（理由上面已经逐条打过日志），也别再让它走一次投影
                {
                    auto skipped = skippedMethods.find(shape.name);
                    if (skipped != skippedMethods.end() && skipped->second.count(method.name) > 0)
                    {
                        continue;
                    }
                }
                std::vector<Projected> params;
                std::string returnType;
                std::string rejectReason;
                bool needsTwoStep = false;
                bool isHandleReturn = false;
                if (!projectMethod(method, &params, &returnType, &rejectReason, &needsTwoStep, &isHandleReturn))
                {
                    continue;
                }
                // 判据看两件事叠加：
                // ① ABI 形状（原来那条）——抓"两个不同拼写投影成同一串字节"的隐式转换重载；
                // ② 值类 / 序列化块 / 已绑定接口 / 内存窗参数的**托管类型**后缀 ——它们在同一串
                //    ABI 字节上仍然对应不同的托管类型（`ScheduleData(ImageHandle…)` 与
                //    `ScheduleData(BufferHandle…)`、`AddPass(RenderPass)` 与 `AddPass(ComputeBatch)`），
                //    不是重载冲突。只保留 ABI 形状会误杀这些（实测）。
                std::string key = method.name + "|" + returnType + "(";
                for (Projected const& projected : params)
                {
                    key += projected.type + ",";
                }
                key += ")";
                auto managedToken = [](std::string const& bare) -> std::string {
                    const std::string shortName = unqualifiedName(bare);
                    if (g_valueTypes.count(shortName) > 0)
                    {
                        return "VC:" + shortName;
                    }
                    if (g_serializedTypes.count(shortName) > 0)
                    {
                        return "BLK:" + shortName;
                    }
                    if (g_bindableInterfaceNames.count(shortName) > 0 &&
                        (hasPrefix(bare, "castl::shared_ptr") || hasPrefix(bare, "std::shared_ptr") ||
                         hasPrefix(bare, "castl::weak_ptr") || hasPrefix(bare, "std::weak_ptr")))
                    {
                        return "IF:" + shortName;
                    }
                    return std::string();
                };
                for (auto const& parameter : method.params)
                {
                    if (method.injectedParams.count(parameter.second) > 0)
                    {
                        continue;
                    }
                    const std::string token = managedToken(bareType(parameter.first));
                    if (!token.empty())
                    {
                        key += "|" + token;
                    }
                }
                auto existing = seenSignatures.find(key);
                if (existing != seenSignatures.end())
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝 %s::%s 与 %s —— ③ 依赖隐式转换的重载：两者投影成同一个 C ABI 签名\n",
                                 shape.name.c_str(), method.name.c_str(), existing->second.c_str());
                    ++rejected;
                }
                else
                {
                    seenSignatures[key] = method.name;
                }
            }
        }
        // ---- 共用句柄表（任务 5.11 / design D12 乙）：整个进程一张，表项带接口标签 ----
        // 每个接口的产物 `#include "BindingHandleTable.h"`（相对包含 —— 它与产物同目录，
        // 调用方不需要为它多开一条 include 路径）。写成 inline 函数里的函数级 static，
        // 保证"一个进程只有一份表"（普通 static 会按翻译单元各来一份）。
        {
            std::string table;
            table += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
            table += "// 进程级句柄表：**所有接口共用这一张**，表项带接口标签。\n";
            table += "// 为什么不能每个接口一张：句柄 = (代际 << 48) | 序号，而每个接口的序号都从 1 开始，\n";
            table += "// 两张独立的表会发出**数字完全相同**的句柄；把 A 表的句柄交给 B 表的释放入口，B 表会\n";
            table += "// 命中一个同号的活项并把它清掉 —— 销毁的是一个无关对象（实测，见 tasks.md 5.11）。\n";
            table += "#pragma once\n";
            table += "#include <stdint.h>\n#include <memory>\n#include <mutex>\n#include <vector>\n\n";
            // D11：序列化块里的值类容器元素 / 接口对象引用按句柄反查绑定对象表。
            // 解析器由 thunk 注入（它持有 `self`）；`expectedType` / `fieldPath` 只用于错误点名。
            table += "namespace cacore::codegen\n{\n";
            table += "    struct BindingHandleResolver\n    {\n";
            table += "        std::shared_ptr<void> (*resolve)(void* user, uint64_t handle, "
                     "char const* expectedType, char const* fieldPath) = nullptr;\n";
            table += "        void* user = nullptr;\n";
            table += "    };\n}\n\n";
            table += "namespace cascaded_bindings\n{\n";
            table += "    struct HandleEntry\n    {\n";
            table += "        void const* object;          // 对象指针（弱/借用句柄可能是空或已失效）\n";
            table += "        uint32_t generation;         // 代际：释放一次进位一次，旧句柄因此失效\n";
            table += "        uint32_t kind;               // 0 空 / 1 强 / 2 弱 / 3 已退休 / 4 借用\n";
            table += "        void const* interfaceTag;    // 发行它的那个接口（每个产物一个独有地址）\n";
            table += "        char const* issuerType;      // 发行者的完整限定名（诊断用）\n";
            table += "        char const* targetType;      // 目标对象的完整限定名（空 = 非接口数据对象）\n";
            table += "        bool hasWitness;             // 借用句柄：有没有拿到存活见证者\n";
            table += "        uint64_t witnessHandle;      // 借用句柄：发行者在表里那一个句柄值（0 = 无见证）\n";
            table += "        std::shared_ptr<void> strong; // 强句柄：生成物持有强引用\n";
            table += "        std::weak_ptr<void> weak;     // 弱句柄：只观察；借用句柄：见证者\n";
            table += "    };\n\n";
            table += "    // 表**故意不析构**：它是带 shared_ptr / weak_ptr 的静态容器，进程退出时别的静态\n";
            table += "    // （CRT 的锁与堆）可能已经拆了，实测会在 dynamic atexit destructor 里 ACCESS_VIOLATION。\n";
            table += "    inline std::vector<HandleEntry>& HandleTable()\n    {\n";
            table += "        static std::vector<HandleEntry>& table = *new std::vector<HandleEntry>();\n";
            table += "        return table;\n    }\n";
            table += "    // 所有读写都在这一把锁里（任务 4.3）。递归是因为释放内部会调一次查表。\n";
            table += "    inline std::recursive_mutex& HandleMutex()\n    {\n";
            table += "        static std::recursive_mutex& mutex = *new std::recursive_mutex();\n";
            table += "        return mutex;\n    }\n";
            table += "}\n";
            const std::string tablePath = std::string(outDir) + "/BindingHandleTable.h";
            std::ofstream tableOut(tablePath, std::ios::binary);
            tableOut << table;
            if (!tableOut)
            {
                std::fprintf(stderr, "CodeGen: cannot write '%s'\n", tablePath.c_str());
                return 2;
            }
        }
        // ---- 反向接口发射（csharp-reverse-interface-binding）：C# 实现 C++ ----
        // 反向产物与正向产物是**两套形状**（正向：IDisposable 表面类 + Api_v2；反向：抽象基类 +
        // 代理 + 注册 glue），所以走独立分支；检查脚本靠产物里的 `CODEGEN-REVERSE-BINDING` 类别标记
        // 区分两者，并对反向用替代判据（design D9）。
        // `reverseKnownNames`：主头 + 包含闭包里出现过 `CA_BIND_REVERSE()` 的接口名 ——
        // IResourceFactory::Create() 返回 IResource*，而 IResource 的标注在它 include 的头里。
        // （在解析期就收好了；这里直接用于反向发射。）
        int reverseMethodsTotal = 0;
        if (!reverseShapes.empty())
        {
            if (const int runtimeResult = emitReverseRuntime(outDir); runtimeResult != 0)
            {
                return runtimeResult;
            }
            std::map<std::string, InterfaceShape const*> reverseByShort;
            for (InterfaceShape const& shape : reverseShapes)
            {
                reverseByShort[shape.name] = &shape;
            }
            for (InterfaceShape const& shape : reverseShapes)
            {
                auto marker = std::find_if(markers.begin(), markers.end(),
                                           [&shape](BoundMarker const& candidate) {
                                               return candidate.reverse && candidate.interfaceName == shape.name;
                                           });
                if (marker == markers.end())
                {
                    std::fprintf(stderr, "CodeGen: 反向 %s 找不到对应的 CA_BIND_REVERSE 标注\n",
                                 shape.qualifiedName.c_str());
                    return 1;
                }
                int emitted = 0;
                const int result = emitReverseProducts(shape, *marker, reverseByShort, reverseKnownNames,
                                                       filePath, handleIndexBits, handleGenerationBits, outDir,
                                                       &emitted);
                if (result != 0)
                {
                    return result;
                }
                reverseMethodsTotal += emitted;
                ++products;
            }
        }

        for (InterfaceShape const& shape : shapes)
        {
            std::string cpp;
            // 本接口的限定名线形（D2/D6）：C ABI 槽名、产物文件名与文件内 C++ 辅助标识符统一用它作前缀。
            const std::string linear = qualifiedLinearName(shape.qualifiedName);
            // 序列化块四条错误码只在"确实用了序列化块"的接口产物里出现：既有接口的产物
            // 因此逐字节不变（本批的回归硬锚），而用了块的接口仍能逐项点名。
            const bool usesSerializedBlocks = [&shape]() {
                for (MethodSignature const& method : shape.methods)
                {
                    for (auto const& parameter : method.params)
                    {
                        if (g_serializedTypes.count(bareType(parameter.first)) > 0)
                        {
                            return true;
                        }
                    }
                }
                return false;
            }();
            cpp += "// 由 CodeGen --emit-binding 生成，勿手改。\n#pragma once\n#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n#include <string.h>\n#include <exception>\n#include <mutex>\n#include <vector>\n#include <type_traits>\n";
            // 接口自己的声明从哪来：写源头的文件名（用基名，靠调用方的 include 目录解析）。
            // 真实接口（例如 IWindow）在模块自己的头目录里，构建时那条目录一定在 include 路径上。
            {
                std::string base = filePath != nullptr ? std::string(filePath) : std::string();
                const std::size_t slash = base.find_last_of("/\\");
                if (slash != std::string::npos)
                {
                    base = base.substr(slash + 1);
                }
                cpp += "#include \"" + base + "\"\n";
            }
            // 共用句柄表放在接口头**之后**：`check_bindings.py` 用"产物里第一条 `#include "..."`
            // 找接口头（要按它读纯虚方法），抢在它前面会把那条判据读成空表。
            cpp += "#include \"BindingHandleTable.h\"\n";
            // 序列化块记录的 C++ 编解码是跨接口共享的一份头（同一次生成的产物；#pragma once 保证
            // 一个翻译单元只展开一次 —— CoreTests 会把多个 .Bindings.cpp 直接 include 进同一个 TU）。
            for (auto const& entry : g_serializedTypes)
            {
                bool used = false;
                for (MethodSignature const& method : shape.methods)
                {
                    for (auto const& parameter : method.params)
                    {
                        if (bareType(parameter.first) == entry.first)
                        {
                            used = true;
                            break;
                        }
                    }
                    // 记录两段式（D8）：返回位置指向该记录时也要它的编解码器（Len/Copy 伴生用）。
                    if (!used && method.returnSpelling.find('*') != std::string::npos &&
                        unqualifiedName(bareType(method.returnSpelling)) == entry.first &&
                        g_recordReturnTypes.count(entry.first) > 0)
                    {
                        used = true;
                    }
                    if (used)
                    {
                        break;
                    }
                }
                if (used)
                {
                    cpp += "#include \"" + qualifiedLinearName(entry.second.qualifiedName) +
                           ".BindingCodec.h\"\n";
                }
            }
            // 注入钩子的声明放在全局作用域：夹具头里可能把它声明在命名空间内，而 extern "C"
            // 的名字查找仍按命名空间走，thunk 在全局作用域里找不到（实测 C3861）。
            for (MethodSignature const& method : shape.methods)
            {
                for (std::string const& parameterName : method.injectedParams)
                {
                    cpp += "extern \"C\" void* " +
                           injectHookName(shape.qualifiedName, method.name, parameterName) + "();\n";
                }
            }
            cpp += "\n";
            // 任务 5.1 的两个小工具：把短名补成接口所在命名空间里的限定名；按 C++ 名或 C 镜像名查镜像类型。
            auto qualify = [&shape](std::string const& shortName) -> std::string
            {
                if (shortName.find("::") != std::string::npos)
                {
                    return shortName;
                }
                const std::size_t split = shape.qualifiedName.rfind("::");
                return split == std::string::npos ? shortName
                                                  : shape.qualifiedName.substr(0, split + 2) + shortName;
            };
            const std::vector<MirrorType>& mirrors = mirrorsByInterface[shape.name];
            auto findMirrorByCpp = [&mirrors](std::string const& name) -> MirrorType const*
            {
                for (MirrorType const& mirror : mirrors)
                {
                    // `cppName` 是**带命名空间的全名**（`ca_bind_fixture::BindScalarThing`），
                    // 而接口里写的短名不带命名空间 —— 两边都要按短名比。
                    if (unqualifiedName(mirror.cppName) == name || mirror.cName == name)
                    {
                        return &mirror;
                    }
                }
                return nullptr;
            };
            std::string cs;
            cs += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
            cs += "using System;\n";
            cs += "using System.Runtime.InteropServices;\n";
            cs += "using Scripting.Managed;\n\n";
            cs += "namespace " + cSharpNamespaceOf(shape.qualifiedName) + "\n{\n";
            // 公开记录 / 枚举按类型另发共享 `.Bindings.cs`（任务 2.3）：接口产物里只留 C 镜像与转换。
            cs += "    /// <summary>" + shape.name + " 的句柄：C 侧是不透明 uint64，0 表示无效</summary>\n";
            cs += "    [StructLayout(LayoutKind.Sequential)]\n";
            // 句柄类型是 native 细节（任务 5.2 验收②：public 类型里不许出现 ulong 句柄），降为 internal
            cs += "    internal readonly struct " + shape.name + "Handle\n    {\n";
            cs += "        public readonly ulong Raw;\n";
            cs += "        public " + shape.name + "Handle(ulong raw) { Raw = raw; }\n";
            cs += "        public bool IsValid => Raw != 0UL;\n";
            cs += "    }\n\n";
            cs += "    internal static unsafe class " + shape.name + "Bindings\n    {\n";
            // C 形状的镜像结构体嵌在 Bindings 里：每个产物各有一套，进程里不会因为多个产物而重名。
            emitMirrorCSharpStructs(&cs, mirrors);

            // 错误通道**不再是这两个裸字段**（任务 5.10 / design D11）：它们是"声明了但全仓
            // 没有一处赋值"的空指针，托管侧根本读不到。现在两个槽进了函数表，生成物里只剩
            // 契约常量 `<接口>Api_v2_last_error_code` / `<接口>Api_v2_last_error_text`。

            int emitted = 0;
            // 每个接口一对错误通道：托管侧靠它把"异常 → 错误码 + 文本"读回来（异常绝不穿过 C ABI）。
            cpp += "extern \"C\" int32_t " + linear + "__LastErrorCode(void* self);\n";
            cpp += "extern \"C\" int32_t " + linear + "__LastErrorText(void* self, char* dst, uint64_t cap);\n";
            // 句柄的解析/释放也要进表（任务 5.2）：托管表面的 `Dispose` 与"解出 self"都靠它 ——
            // 这两个入口不在表里，托管侧连函数地址都拿不到（它啃不动 DLL 里的导出符号名）。
            cpp += "extern \"C\" void* " + linear + "__ResolveHandle(void* self, uint64_t handle);\n";
            cpp += "extern \"C\" void " + linear + "__ReleaseHandle(void* self, uint64_t handle);\n";
            // 目标接口入口：追加在函数表末尾（任务 3.2），ABI 版本不变。
            cpp += "extern \"C\" void* " + linear + "__ResolveTargetHandle(void* self, uint64_t handle);\n";
            cpp += "extern \"C\" void " + linear + "__ReleaseTargetHandle(void* self, uint64_t handle);\n";
            // 布局清单（任务 2.5）：镜像结构体 + 编译期断言。必须**在方法声明的循环之前**发出 ——
            // 声明里会用到镜像类型，放在后面就是"先用后定义"（实测 C4430 / C2378）。
            emitLayoutManifest(&cpp, &cs, linear, mirrors);
            // 函数表用一个**槽列表**描述，而不是边循环边拼字符串：表头那四个槽（错误通道 + 句柄
            // 解析/释放）与两段式伴生的两个槽都不是"接口方法"，但都要进表，只有列表才排得一致。
            struct TableSlot
            {
                std::string name;   // 表里的成员名（与 C ABI 入口同名，C++ 与 C# 两侧靠它对齐）
                std::string member; // 结构体里的完整成员声明
                std::string init;   // 初始化表达式
                std::string csType; // C# 侧 delegate* 的泛型参数表（最后一项是返回类型）
            };
            std::vector<TableSlot> tableSlots;
            tableSlots.push_back({"LastErrorCode", "int32_t (*LastErrorCode)(void* self)",
                                  "&" + linear + "_readErrorCode", "void*, int"});
            tableSlots.push_back({"LastErrorText", "void (*LastErrorText)(void* self, char* dst, uint64_t cap)",
                                  "&" + linear + "_readErrorText", "void*, byte*, ulong, void"});
            tableSlots.push_back({"ResolveHandle", "void* (*ResolveHandle)(void* self, uint64_t handle)",
                                  "&" + linear + "__ResolveHandle", "void*, ulong, void*"});
            tableSlots.push_back({"ReleaseHandle", "void (*ReleaseHandle)(void* self, uint64_t handle)",
                                  "&" + linear + "__ReleaseHandle", "void*, ulong, void"});
            std::string thunkDefs;     // thunk 实现
            std::string externDefs;    // 导出入口（extern "C" 的定义）
            std::string lateDefs;      // 必须排在句柄表实现之后的定义（Release 之类）
            struct SurfaceMethod
            {
                MethodSignature const* method = nullptr;
                std::vector<Projected> params;
                std::string returnType;
                bool needsTwoStep = false;
                bool isHandleReturn = false;
                bool isCallback = false;
                MethodCallbackInfo callback;
                // 记录两段式（D8）：非空 = 返回位置指向该记录，托管侧走 Len/Copy 取序列化副本。
                std::string twoStepRecordShort;
                // 返回位置标量容器两段式（本批 1.6 / D13）：非空 = 元素宽度类型，走 Count/Copy。
                std::string twoStepScalarElement;
            };
            std::vector<SurfaceMethod> surfaceMethods;
            // 回调方法（按声明顺序）要在方法循环之前就知道：注册号里的 method 索引与注销分发都靠它。
            struct CallbackMethod
            {
                std::string name;
                MethodCallbackInfo info;
                std::size_t index = 0;
                std::string originalType;   // 原始 `castl::function<void(...)>` 拼写（清空与构造都用它）
            };
            std::vector<CallbackMethod> callbackMethods;
            {
                for (MethodSignature const& method : shape.methods)
                {
                    if (method.abiName.empty())
                    {
                        continue;   // 2.2 过渡：同名第二槽不绑
                    }
                    auto skipped = skippedMethods.find(shape.name);
                    if (skipped != skippedMethods.end() && skipped->second.count(method.name) > 0)
                    {
                        continue;
                    }
                    MethodCallbackInfo info;
                    std::string whyNot;
                    if (analyzeCallbackMethod(method, &info, &whyNot))
                    {
                        callbackMethods.push_back(
                            { method.name, info, callbackMethods.size(),
                              callbackCppFunctionType(info.shape,
                                                      namespacePrefixOf(shape.qualifiedName)) });
                    }
                }
            }
            const bool hasCallbacks = !callbackMethods.empty();

            // 哪些接口会作为回调里的 `T*` 出现（design D3）：它们的产品要发 `<T>BorrowScope`，
            // 表面对象的租约带一个作用域，回调返回后统一失效。这个集合要在逐接口循环之前算好 ——
            // `IWindow` 的产品可能先于 `IWindowSystem` 生成。
            std::set<std::string> borrowableInterfaces;
            for (InterfaceShape const& anyShape : shapes)
            {
                for (MethodSignature const& anyMethod : anyShape.methods)
                {
                    MethodCallbackInfo info;
                    std::string whyNot;
                    if (!analyzeCallbackMethod(anyMethod, &info, &whyNot))
                    {
                        continue;
                    }
                    for (CallbackArg const& argument : info.shape.args)
                    {
                        if (!argument.interfaceName.empty())
                        {
                            borrowableInterfaces.insert(argument.interfaceName);
                        }
                    }
                }
            }
            const bool isBorrowable = borrowableInterfaces.count(shape.name) > 0;

            // ---- 回调注册表与桥（本 change 任务 2.1 / 4.1–4.3）----
            // 每个接口一份：注册号标识 (self, 方法, 本次注册)，注销只清仍匹配 current 的那一项；
            // 活动标记挡"回调执行期间替换/清空同一个注册"；回调错误先进独立槽，下一次主线程调用再上报。
            std::string callbackSupport;
            if (hasCallbacks)
            {
                const std::string& I = linear;
                callbackSupport += "// ---- 回调注册表（" + I + "）----\n";
                callbackSupport += "typedef struct " + I + "CallbackEntry {\n";
                callbackSupport += "    uint64_t registration;   // 0 不用；单调递增，标识本次注册\n";
                callbackSupport += "    void const* self;        // 注册它的那个实例\n";
                callbackSupport += "    uint32_t method;         // 方法序号（注销时用来分发到正确的清空入口）\n";
                callbackSupport += "    int32_t active;          // 正在执行（重入保护）\n";
                callbackSupport += "    int32_t current;         // 是该 (self, 方法) 的现行注册\n";
                callbackSupport += "} " + I + "CallbackEntry;\n";
                callbackSupport += "static std::vector<" + I + "CallbackEntry>& " + I +
                                   "_callbackEntries = *new std::vector<" + I + "CallbackEntry>();\n";
                callbackSupport += "static std::recursive_mutex " + I + "_callbackMutex;\n";
                callbackSupport += "static uint64_t " + I + "_callbackNextRegistration = 0u;\n";
                callbackSupport += "static thread_local uint64_t " + I + "_callbackActiveRegistration = 0u;\n";
                callbackSupport += "typedef struct " + I + "CallbackErrorSlot {\n";
                callbackSupport += "    void const* self;\n    int32_t code;\n    char text[256];\n";
                callbackSupport += "} " + I + "CallbackErrorSlot;\n";
                callbackSupport += "static std::vector<" + I + "CallbackErrorSlot>& " + I +
                                   "_callbackErrors = *new std::vector<" + I + "CallbackErrorSlot>();\n";
                callbackSupport += "static std::recursive_mutex " + I + "_callbackErrorMutex;\n";
                // 注销时用的清空分发：每个回调方法一个 case，类型必须与声明逐字一致
                callbackSupport += "static void " + I + "_clearNativeCallback(void* self, uint32_t method)\n{\n";
                callbackSupport += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                callbackSupport += "    try\n    {\n        switch (method)\n        {\n";
                for (CallbackMethod const& callback : callbackMethods)
                {
                    callbackSupport += "        case " + std::to_string(callback.index) + ": object->" +
                                       callback.name + "(castl::function<" + callback.originalType +
                                       ">{}); break;\n";
                }
                callbackSupport += "        default: break;\n        }\n    }\n";
                callbackSupport += "    catch (...) { " + I + "_setError(self, " + I +
                                   "_ERROR_CALLBACK_EXCEPTION, \"原生实现抛出了 C++ 异常\"); }\n}\n";
                // 活动标记 / 重入检查 / 注册 / 清空
                callbackSupport += "static void " + I + "_beginCallback(uint64_t registration)\n{\n";
                callbackSupport += "    std::lock_guard<std::recursive_mutex> guard(" + I + "_callbackMutex);\n";
                callbackSupport += "    for (auto& entry : " + I + "_callbackEntries)\n    {\n";
                callbackSupport += "        if (entry.registration == registration && entry.current != 0)\n";
                callbackSupport += "        {\n            entry.active = 1;\n            " + I +
                                   "_callbackActiveRegistration = registration;\n            return;\n        }\n    }\n}\n";
                callbackSupport += "static void " + I + "_endCallback(uint64_t registration)\n{\n";
                callbackSupport += "    std::lock_guard<std::recursive_mutex> guard(" + I + "_callbackMutex);\n";
                callbackSupport += "    for (auto& entry : " + I + "_callbackEntries)\n    {\n";
                callbackSupport += "        if (entry.registration == registration) { entry.active = 0; }\n    }\n";
                callbackSupport += "    if (" + I + "_callbackActiveRegistration == registration)\n    {\n        " + I +
                                   "_callbackActiveRegistration = 0u;\n    }\n}\n";
                callbackSupport += "static int32_t " + I +
                                   "_callbackModifyBlocked(void const* self, uint32_t method)\n{\n";
                callbackSupport += "    std::lock_guard<std::recursive_mutex> guard(" + I + "_callbackMutex);\n";
                callbackSupport += "    const uint64_t active = " + I + "_callbackActiveRegistration;\n";
                callbackSupport += "    if (active == 0u) { return 0; }\n";
                callbackSupport += "    for (auto const& entry : " + I + "_callbackEntries)\n    {\n";
                callbackSupport += "        if (entry.registration == active && entry.method == method)\n";
                callbackSupport += "        {\n            " + I + "_setError(self, " + I +
                                   "_ERROR_CALLBACK_REENTRANT, \"回调执行期间不允许替换或清空同一个回调注册\");\n";
                callbackSupport += "            return 1;\n        }\n    }\n    return 0;\n}\n";
                callbackSupport += "static void " + I +
                                   "_supersedeCallback(void const* self, uint32_t method)\n{\n";
                callbackSupport += "    std::lock_guard<std::recursive_mutex> guard(" + I + "_callbackMutex);\n";
                callbackSupport += "    for (auto& entry : " + I + "_callbackEntries)\n    {\n";
                callbackSupport += "        if (entry.self == self && entry.method == method) { entry.current = 0; }\n";
                callbackSupport += "    }\n}\n";
                callbackSupport += "static uint64_t " + I + "_registerCallback(void const* self, uint32_t method)\n{\n";
                callbackSupport += "    std::lock_guard<std::recursive_mutex> guard(" + I + "_callbackMutex);\n";
                callbackSupport += "    for (auto& entry : " + I + "_callbackEntries)\n    {\n";
                callbackSupport += "        if (entry.self == self && entry.method == method) { entry.current = 0; }\n";
                callbackSupport += "    }\n";
                callbackSupport += "    " + I + "CallbackEntry entry{};\n    entry.registration = ++" + I +
                                   "_callbackNextRegistration;\n    entry.self = self;\n    entry.method = method;\n";
                callbackSupport += "    entry.current = 1;\n    " + I + "_callbackEntries.push_back(entry);\n";
                callbackSupport += "    return entry.registration;\n}\n";
                callbackSupport += "static void " + I + "_unregisterCallback(void* self, uint64_t registration)\n{\n";
                callbackSupport += "    uint32_t method = 0u;\n    void* owner = nullptr;\n    int32_t blocked = 0;\n";
                callbackSupport += "    {\n        std::lock_guard<std::recursive_mutex> guard(" + I + "_callbackMutex);\n";
                callbackSupport += "        for (auto& entry : " + I + "_callbackEntries)\n        {\n";
                callbackSupport += "            if (entry.registration != registration) { continue; }\n";
                callbackSupport += "            if (entry.current == 0) { return; }   // 旧注册号：静默无操作\n";
                callbackSupport += "            if (entry.active != 0) { blocked = 1; break; }\n";
                callbackSupport += "            method = entry.method;\n            owner = const_cast<void*>(entry.self);\n";
                callbackSupport += "            entry.current = 0;\n            break;\n        }\n    }\n";
                callbackSupport += "    if (blocked != 0)\n    {\n        " + I + "_setError(self, " + I +
                                   "_ERROR_CALLBACK_REENTRANT, \"回调执行期间不允许注销正在执行的回调\");\n        return;\n    }\n";
                callbackSupport += "    if (owner == nullptr) { return; }\n    " + I +
                                   "_clearNativeCallback(owner, method);\n}\n";
                // 回调错误槽：桥里捕获的异常先落这里，下一次主线程调用再搬到统一错误通道
                callbackSupport += "static void " + I +
                                   "_setCallbackError(void const* self, int32_t code, char const* text)\n{\n";
                callbackSupport += "    std::lock_guard<std::recursive_mutex> guard(" + I + "_callbackErrorMutex);\n";
                callbackSupport += "    auto* slot = static_cast<" + I + "CallbackErrorSlot*>(nullptr);\n";
                callbackSupport += "    for (auto& candidate : " + I + "_callbackErrors)\n    {\n";
                callbackSupport += "        if (candidate.self == self) { slot = &candidate; break; }\n    }\n";
                callbackSupport += "    if (slot == nullptr)\n    {\n        " + I +
                                   "_callbackErrors.push_back({self, 0, {0}});\n        slot = &" + I +
                                   "_callbackErrors.back();\n    }\n";
                callbackSupport += "    slot->code = code;\n    slot->text[0] = 0;\n";
                callbackSupport += "    if (text != nullptr)\n    {\n";
                callbackSupport += "        const std::size_t room = sizeof(slot->text) - 1u;\n";
                callbackSupport += "        std::size_t length = 0u;\n";
                callbackSupport += "        while (length < room && text[length] != 0) { ++length; }\n";
                callbackSupport += "        ::memcpy(slot->text, text, length);\n        slot->text[length] = 0;\n    }\n}\n";
                callbackSupport += "static void " + I + "_attachCallbackError(void const* self)\n{\n";
                callbackSupport += "    int32_t code = 0;\n    char text[256] = {0};\n";
                callbackSupport += "    {\n        std::lock_guard<std::recursive_mutex> guard(" + I +
                                   "_callbackErrorMutex);\n";
                callbackSupport += "        for (auto& slot : " + I + "_callbackErrors)\n        {\n";
                callbackSupport += "            if (slot.self != self || slot.code == 0) { continue; }\n";
                callbackSupport += "            code = slot.code;\n            ::memcpy(text, slot.text, sizeof(text));\n";
                callbackSupport += "            slot.code = 0;\n            slot.text[0] = 0;\n            break;\n        }\n    }\n";
                callbackSupport += "    if (code != 0) { " + I + "_setError(self, code, text); }\n}\n";
                callbackSupport += "extern \"C\" void " + I +
                                   "__ReportCallbackError(void* self, int32_t code, char const* text, uint64_t textLen)\n{\n";
                callbackSupport += "    std::lock_guard<std::recursive_mutex> guard(" + I + "_callbackErrorMutex);\n";
                callbackSupport += "    auto* slot = static_cast<" + I + "CallbackErrorSlot*>(nullptr);\n";
                callbackSupport += "    for (auto& candidate : " + I + "_callbackErrors)\n    {\n";
                callbackSupport += "        if (candidate.self == self) { slot = &candidate; break; }\n    }\n";
                callbackSupport += "    if (slot == nullptr)\n    {\n        " + I +
                                   "_callbackErrors.push_back({self, 0, {0}});\n        slot = &" + I +
                                   "_callbackErrors.back();\n    }\n";
                callbackSupport += "    slot->code = code;\n    slot->text[0] = 0;\n";
                callbackSupport += "    if (text != nullptr)\n    {\n";
                callbackSupport += "        const std::size_t room = sizeof(slot->text) - 1u;\n";
                callbackSupport += "        const std::size_t length = (textLen < room) ? static_cast<std::size_t>(textLen) : room;\n";
                callbackSupport += "        ::memcpy(slot->text, text, length);\n        slot->text[length] = 0;\n    }\n}\n";
            }

            for (MethodSignature const& method : shape.methods)
            {
                if (method.abiName.empty())
                {
                    continue;   // 2.2 过渡：同名第二槽不绑
                }
                // 带理由排除的方法：不出绑定（理由上面已经逐条打过日志），也别再让它走一次投影
                {
                    auto skipped = skippedMethods.find(shape.name);
                    if (skipped != skippedMethods.end() && skipped->second.count(method.name) > 0)
                    {
                        continue;
                    }
                }
                std::vector<Projected> params;
                std::string returnType;
                std::string rejectReason;
                bool needsTwoStep = false;
                bool isHandleReturn = false;
                bool isCallback = false;
                std::string twoStepRecordShort;
                std::string twoStepScalarElement;
                if (!projectMethod(method, &params, &returnType, &rejectReason, &needsTwoStep, &isHandleReturn,
                                   &isCallback, &twoStepRecordShort, &twoStepScalarElement))
                {
                    std::fprintf(stderr, "CodeGen: 拒绝 %s::%s —— %s\n", shape.name.c_str(),
                                 method.name.c_str(), rejectReason.c_str());
                    ++rejected;
                    continue;
                }
                const std::string abiName = method.abiName;
                MethodCallbackInfo callbackInfo;
                std::size_t callbackIndex = static_cast<std::size_t>(-1);
                if (isCallback)
                {
                    for (CallbackMethod const& candidate : callbackMethods)
                    {
                        if (candidate.name == method.name)
                        {
                            callbackInfo = candidate.info;
                            callbackIndex = candidate.index;
                            break;
                        }
                    }
                }
                // 托管表面（任务 5.2 / 5.6）要按**原始 C++ 声明**出成员，所以这里把投影结果连
                // 同原始签名一起收起来，循环结束后统一发射。
                surfaceMethods.push_back(
                    { &method, params, returnType, needsTwoStep, isHandleReturn, isCallback, callbackInfo,
                      twoStepRecordShort, twoStepScalarElement });
                // ---- 任务 5.1：两类"成员级映射"形状要先在这里认出元素类型 ----
                // 镜像结构体按值返回：C 侧 `<T>C`、C++ 侧 `T`，靠 mirrors 里的字段名逐成员搭桥。
                const MirrorType* mirrorReturn = findMirrorByCpp(bareType(method.returnSpelling));
                if (mirrorReturn != nullptr && returnType != mirrorReturn->cName)
                {
                    // 只有"投影结果就是那个镜像类型"才算这一类（其余形状走下面的分支）
                    mirrorReturn = nullptr;
                }
                // 入参里是否也有纯标量记录（`SetMonitorInfo(MonitorInfo const&)` 返回 void，但同样要搬运）
                bool hasMirrorParam = false;
                for (auto const& parameter : method.params)
                {
                    if (findMirrorByCpp(bareType(parameter.first)) != nullptr)
                    {
                        hasMirrorParam = true;
                        break;
                    }
                }
                // 容器入参：唯一形参是 **`castl::vector<T> const&`**（入参），且 T 能对上镜像类型。
                // **只认 const&**：非 const 的 `vector<T>&` 是**出参**（`dst + cap` 两段式），
                // 它的协议（容量不够怎么办、要不要报错）本轮没定，按出参当入参生成会把调用方的
                // 目标缓冲静默丢空 —— 所以那种形状仍然留桩，理由写在 design.md。
                const MirrorType* containerIn = nullptr;
                if (method.params.size() == 1 &&
                    method.params[0].first.find("const") != std::string::npos)
                {
                    const std::string paramBare = bareType(method.params[0].first);
                    const std::size_t open = paramBare.find('<');
                    const std::size_t close = paramBare.rfind('>');
                    if (hasPrefix(paramBare, "castl::vector") && open != std::string::npos &&
                        close != std::string::npos && close > open)
                    {
                        containerIn = findMirrorByCpp(trimCopy(paramBare.substr(open + 1, close - open - 1)));
                        if (params.size() != 2)
                        {
                            containerIn = nullptr;
                        }
                    }
                }
                // 两段式（字符串返回）：只发两个伴生，**不发**直出入口、不进函数表、不发直出 thunk。
                // 三个都不发才自洽 —— 只删表槽会让空体 thunk 留着被桩判据算成桩，整段排除又会把
                // 刚发的伴生声明一起删掉（任务 2.5 / design D5）。
                const std::string twoStepBare = bareType(method.returnSpelling);
                const bool stringTwoStep = needsTwoStep &&
                    (hasPrefix(twoStepBare, "castl::string_view") || hasPrefix(twoStepBare, "std::string_view") ||
                     twoStepBare == "castl::string" || twoStepBare == "std::string");
                if (stringTwoStep)
                {
                    cpp += "extern \"C\" uint64_t " + linear + "__" + abiName + "Len(void* self);\n";
                    cpp += "extern \"C\" void " + linear + "__Copy" + abiName +
                           "(void* self, char* dst, uint64_t cap);\n";
                    // 两个伴生**各自独立**取一次实现的值：不依赖跨调用的暂存状态
                    lateDefs += "extern \"C\" uint64_t " + linear + "__" + abiName +
                                "Len(void* self)\n{\n";
                    // 两个伴生也是"入口"：不先清就会把上一次调用的失败码算到这次头上
                    lateDefs += "    " + linear + "_clearError(self);\n";
                    lateDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    lateDefs += "    return static_cast<uint64_t>(object->" + method.name + "().size());\n}\n";
                    lateDefs += "extern \"C\" void " + linear + "__Copy" + abiName +
                                "(void* self, char* dst, uint64_t cap)\n{\n";
                    lateDefs += "    " + linear + "_clearError(self);\n";
                    // 容量是无符号的：`cap - 1` 在 0 时会下溢成最大值，必须先判
                    lateDefs += "    if (cap == 0u || dst == nullptr) { return; }\n";
                    lateDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    lateDefs += "    const auto value = object->" + method.name + "();\n";
                    lateDefs += "    const std::size_t length = static_cast<std::size_t>(value.size());\n";
                    lateDefs += "    const std::size_t copied = (length < cap - 1u) ? length : cap - 1u;\n";
                    lateDefs += "    ::memcpy(dst, value.data(), copied);\n";
                    lateDefs += "    dst[copied] = 0;\n}\n";
                    // 两段式伴生也进表（任务 5.2）：托管表面的 `string GetXxx()` 两段都得能调到，
                    // 而它们只是导出符号、不在表里 —— 托管侧拿不到地址。
                    tableSlots.push_back({abiName + "Len", "uint64_t (*" + abiName + "Len)(void* self)",
                                          "&" + linear + "__" + abiName + "Len", "void*, ulong"});
                    tableSlots.push_back({"Copy" + abiName,
                                          "void (*Copy" + abiName +
                                              ")(void* self, char* dst, uint64_t cap)",
                                          "&" + linear + "__Copy" + abiName,
                                          "void*, byte*, ulong, void"});
                    continue;
                }
                // ---- 返回位置标量容器两段式（本批 1.6 / design D13）：`vector<标量> const&` →
                // `Count` + `Copy` 两个伴生。与字符串两段式同族：不发直出槽、两个伴生都进函数表。
                // 空容器：Count=0、Copy 在 dst 为空或 cap=0 时直接返回（不解引用）。
                if (!twoStepScalarElement.empty())
                {
                    if (!method.params.empty())
                    {
                        std::fprintf(stderr,
                                     "CodeGen: 拒绝 %s::%s —— 标量容器两段式只支持无参方法"
                                     "（本批见证形状 GetPassIndices 无参）\n",
                                     shape.name.c_str(), method.name.c_str());
                        ++rejected;
                        continue;
                    }
                    cpp += "extern \"C\" uint64_t " + linear + "__" + abiName + "Count(void* self);\n";
                    cpp += "extern \"C\" void " + linear + "__Copy" + abiName +
                           "(void* self, uint8_t* dst, uint64_t cap);\n";
                    lateDefs += "extern \"C\" uint64_t " + linear + "__" + abiName +
                                "Count(void* self)\n{\n";
                    lateDefs += "    " + linear + "_clearError(self);\n";
                    lateDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    lateDefs += "    return static_cast<uint64_t>(object->" + method.name +
                                "().size());\n}\n";
                    lateDefs += "extern \"C\" void " + linear + "__Copy" + abiName +
                                "(void* self, uint8_t* dst, uint64_t cap)\n{\n";
                    lateDefs += "    " + linear + "_clearError(self);\n";
                    lateDefs += "    if (dst == nullptr || cap == 0u) { return; }\n";
                    lateDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    lateDefs += "    auto const& value = object->" + method.name + "();\n";
                    lateDefs += "    if (value.empty()) { return; }\n";
                    lateDefs += "    const std::size_t bytes = value.size() * sizeof(" +
                                twoStepScalarElement + ");\n";
                    lateDefs += "    const std::size_t limit = static_cast<std::size_t>(cap);\n";
                    lateDefs += "    const std::size_t copied = (bytes < limit) ? bytes : limit;\n";
                    lateDefs += "    ::memcpy(dst, value.data(), copied);\n}\n";
                    tableSlots.push_back({abiName + "Count", "uint64_t (*" + abiName + "Count)(void* self)",
                                          "&" + linear + "__" + abiName + "Count", "void*, ulong"});
                    tableSlots.push_back({"Copy" + abiName,
                                          "void (*Copy" + abiName +
                                              ")(void* self, uint8_t* dst, uint64_t cap)",
                                          "&" + linear + "__Copy" + abiName,
                                          "void*, byte*, ulong, void"});
                    continue;
                }
                // ---- 记录两段式（本批 D8）：`return const Record*` → `Len` + `Copy` 两个伴生 ----
                // 与字符串两段式同线形（同进函数表、不发直出槽）。原生返回空指针 ⇒ Len=0 ⇒ 托管
                // 得到 null，不解引用。参数转换只覆盖本批见证形状（注入 / 文本 / 标量 / 枚举），
                // 其它形状生成期拒绝并点名 —— 不猜。
                if (!twoStepRecordShort.empty())
                {
                    std::string prelude;
                    std::vector<std::string> argExprs;
                    std::size_t abiIndex = 0;
                    bool argsSupported = true;
                    for (std::size_t paramIndex = 0; paramIndex < method.params.size(); ++paramIndex)
                    {
                        const std::string spelling = method.params[paramIndex].first;
                        const std::string bare = bareType(spelling);
                        const std::string name = method.params[paramIndex].second.empty()
                                                     ? std::string("arg") + std::to_string(paramIndex)
                                                     : method.params[paramIndex].second;
                        if (method.injectedParams.count(method.params[paramIndex].second) > 0)
                        {
                            // 同主 thunk：强转用完整限定拼写（短名靠 `using namespace` 才成立，
                            // 生成的 thunk 在全局作用域看不到）。
                            const std::string castSpelling =
                                paramIndex < method.paramCanonicalSpellings.size() &&
                                        !method.paramCanonicalSpellings[paramIndex].empty()
                                    ? method.paramCanonicalSpellings[paramIndex]
                                    : spelling;
                            argExprs.push_back(
                                "static_cast<" + castSpelling + ">(" +
                                injectHookName(method.ownerQualified, method.name,
                                               method.params[paramIndex].second) +
                                "())");
                            continue;
                        }
                        if (isNameHashSpelling(spelling) || isPathHashSpelling(spelling))
                        {
                            const std::string wrapper = isNameHashSpelling(spelling) ? "cacore::NameHash"
                                                                                     : "cacore::PathHash";
                            prelude += "    " + wrapper + " " + name + "Value(castl::string(" + name + ", " +
                                       name + "Len));\n";
                            argExprs.push_back(name + "Value");
                            abiIndex += 2;
                            continue;
                        }
                        const std::string canonicalSpelling =
                            paramIndex < method.paramCanonicalSpellings.size()
                                ? method.paramCanonicalSpellings[paramIndex]
                                : spelling;
                        if (paramIndex < method.paramKinds.size() &&
                            method.paramKinds[paramIndex] == static_cast<int>(CXType_Enum))
                        {
                            EnumInfo const* info = findEnumBySpelling(canonicalSpelling);
                            if (info == nullptr)
                            {
                                argsSupported = false;
                                break;
                            }
                            argExprs.push_back("static_cast<" + info->cppName + ">(" + name + ")");
                            ++abiIndex;
                            continue;
                        }
                        if (isScalarType(bare))
                        {
                            argExprs.push_back(bare == "bool" ? name + " != 0" : name);
                            ++abiIndex;
                            continue;
                        }
                        argsSupported = false;
                        break;
                    }
                    if (!argsSupported || abiIndex != params.size())
                    {
                        std::fprintf(stderr,
                                     "CodeGen: 拒绝 %s::%s —— 记录两段式的参数形状不在本批支持范围内"
                                     "（只支持注入 / 文本 / 标量 / 枚举参数）\n",
                                     shape.name.c_str(), method.name.c_str());
                        ++rejected;
                        continue;
                    }
                    std::string recordQualified = twoStepRecordShort;
                    {
                        // 限定名直接取已建好的序列化记录信息（它由 buildSerializedTypeInfo 用同一
                        // 份游标算过），不在发射期再走一次游标链。
                        auto serialized = g_serializedTypes.find(twoStepRecordShort);
                        if (serialized != g_serializedTypes.end() && !serialized->second.qualifiedName.empty())
                        {
                            recordQualified = serialized->second.qualifiedName;
                        }
                    }
                    std::string paramDecls;
                    std::string argNames;
                    std::string abiParamTypes;
                    std::string csParamTypes = "void*";
                    for (Projected const& projected : params)
                    {
                        paramDecls += ", " + projectedDeclaration(projected);
                        argNames += ", " + projected.name;
                        abiParamTypes += ", " + projected.type;
                        csParamTypes += ", " + toCSharpType(projected.type);
                    }
                    const std::string helper = linear + "_call_" + abiName;
                    cpp += "extern \"C\" uint64_t " + linear + "__" + abiName + "Len(void* self" +
                           paramDecls + ");\n";
                    cpp += "extern \"C\" void " + linear + "__Copy" + abiName + "(void* self" + paramDecls +
                           ", uint8_t* dst, uint64_t cap);\n";
                    // 伴生直接实现成导出函数（与字符串两段式同线形，不带 `_thunk_` 前缀：
                    // 覆盖率判据只认 `_thunk_` 符号，伴生不该出现在那份清单里）。
                    // 共享的原生调用：两个伴生各自调一次（不依赖跨调用的暂存状态）。
                    lateDefs += "static void const* " + helper + "(void* self" + paramDecls + ")\n{\n";
                    lateDefs += prelude;
                    lateDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    lateDefs += "    return static_cast<void const*>(object->" + method.name + "(";
                    for (std::size_t index = 0; index < argExprs.size(); ++index)
                    {
                        lateDefs += (index == 0 ? "" : ", ") + argExprs[index];
                    }
                    lateDefs += "));\n}\n\n";
                    lateDefs += "extern \"C\" uint64_t " + linear + "__" + abiName + "Len(void* self" +
                                paramDecls + ")\n{\n";
                    lateDefs += "    " + linear + "_clearError(self);\n";
                    lateDefs += "    void const* found = " + helper + "(self" + argNames + ");\n";
                    lateDefs += "    if (found == nullptr) { return 0u; }\n";
                    lateDefs += "    std::vector<uint8_t> bytes;\n";
                    lateDefs += "    cacore::codegen::CodeGenBindingCodec<" + recordQualified +
                                ">::Serialize(*static_cast<" + recordQualified + " const*>(found), bytes);\n";
                    lateDefs += "    return static_cast<uint64_t>(bytes.size());\n}\n";
                    lateDefs += "extern \"C\" void " + linear + "__Copy" + abiName + "(void* self" +
                                paramDecls + ", uint8_t* dst, uint64_t cap)\n{\n";
                    lateDefs += "    " + linear + "_clearError(self);\n";
                    lateDefs += "    if (dst == nullptr || cap == 0u) { return; }\n";
                    lateDefs += "    void const* found = " + helper + "(self" + argNames + ");\n";
                    lateDefs += "    if (found == nullptr) { return; }\n";
                    lateDefs += "    std::vector<uint8_t> bytes;\n";
                    lateDefs += "    cacore::codegen::CodeGenBindingCodec<" + recordQualified +
                                ">::Serialize(*static_cast<" + recordQualified + " const*>(found), bytes);\n";
                    lateDefs += "    const uint64_t copied = (static_cast<uint64_t>(bytes.size()) < cap) ? "
                                "static_cast<uint64_t>(bytes.size()) : cap;\n";
                    lateDefs += "    ::memcpy(dst, bytes.data(), static_cast<std::size_t>(copied));\n}\n";
                    tableSlots.push_back({abiName + "Len",
                                          "uint64_t (*" + abiName + "Len)(void* self" + abiParamTypes + ")",
                                          "&" + linear + "__" + abiName + "Len",
                                          csParamTypes + ", ulong"});
                    tableSlots.push_back({"Copy" + abiName,
                                          "void (*Copy" + abiName + ")(void* self" + abiParamTypes +
                                              ", uint8_t* dst, uint64_t cap)",
                                          "&" + linear + "__Copy" + abiName,
                                          csParamTypes + ", byte*, ulong, void"});
                    ++emitted;
                    continue;
                }
                std::string signature = returnType + " " + linear + "__" + abiName + "(void* self";
                for (Projected const& projected : params)
                {
                    signature += ", " + projectedDeclaration(projected);
                }
                signature += ");";
                cpp += "extern \"C\" " + signature + "\n";

                // 两段式伴生：先问长度、再拷出来（矩阵里 string / string_view / 容器返回那一行）
                if (needsTwoStep)
                {
                    cpp += "extern \"C\" uint64_t " + linear + "__" + abiName + "Len(void* self);\n";
                    cpp += "extern \"C\" void " + linear + "__Copy" + abiName +
                           "(void* self, char* dst, uint64_t cap);\n";
                }
                // 句柄的配对释放（矩阵里智能指针返回那一行）：定义成查表 + 代际进位
                if (isHandleReturn)
                {
                    cpp += "extern \"C\" void " + linear + "__Release" + abiName +
                           "(void* self, uint64_t handle);\n";
                    // 配对释放也按 self 记错误码（任务 5.10）：释放失败要能归到调用它的那个实例
                    lateDefs += "extern \"C\" void " + linear + "__Release" + abiName +
                                "(void* self, uint64_t handle)\n{\n    " + linear +
                                "_clearError(self);\n    " + linear +
                                "_releaseHandle(self, handle);\n}\n";
                }

                ++emitted;

                // 函数表成员：句柄作首参（`void* self`），其余按投影后的类型串起来（不含形参名）。
                // 镜像结构体在 C# 侧是 `<接口>Bindings` 里的嵌套 struct，不能按 `void*` 映射。
                auto abiCSharpType = [&mirrors](std::string const& cType) -> std::string {
                    std::string bare = cType;
                    bool pointer = false;
                    while (!bare.empty() && (bare.back() == '*' || bare.back() == ' '))
                    {
                        if (bare.back() == '*')
                        {
                            pointer = true;
                        }
                        bare.pop_back();
                    }
                    if (bare.size() > 6 && bare.compare(bare.size() - 6, 6, " const") == 0)
                    {
                        bare = bare.substr(0, bare.size() - 6);
                    }
                    for (MirrorType const& mirror : mirrors)
                    {
                        if (mirror.cName == bare)
                        {
                            return mirror.cName + (pointer ? "*" : "");
                        }
                    }
                    return toCSharpType(cType);
                };
                std::string paramTypes = "void*";
                std::string csParamTypes = "void*";
                for (Projected const& projected : params)
                {
                    paramTypes += ", " + projected.type;
                    csParamTypes += ", " + abiCSharpType(projected.type);
                }
                tableSlots.push_back({abiName,
                                      returnType + " (*" + abiName + ")(" + paramTypes + ")",
                                      "&" + linear + "_thunk_" + abiName,
                                      csParamTypes + ", " + abiCSharpType(returnType)});

                // thunk 实现：先只覆盖"参数全是标量、返回是 void 或同名标量"的形状；
                // 其它形状（容器 / 两段式 / 句柄 / 回调 / 出参）留给 2.4 / 2.5，这里必须是可见的桩。
                thunkDefs += "static " + returnType + " " + linear + "_thunk_" + abiName + "(void* self";
                for (Projected const& projected : params)
                {
                    thunkDefs += ", " + projectedDeclaration(projected);
                }
                thunkDefs += ")\n{\n";
                // 每个 thunk 入口先清一次（任务 5.10）：读到 0 只可能表示"这次调用没出错"。
                // 上一次失败留下的码不会粘在这里 —— D8 的误抛就是粘出来的。
                thunkDefs += "    " + linear + "_clearError(self);\n";
                bool simpleShape = true;
                for (Projected const& projected : params)
                {
                    // 允许"标量出参指针"（`int32_t* outX` 这种）：调用时解引用即可，不算复杂形状
                    std::string bareParam = projected.type;
                    if (!bareParam.empty() && bareParam.back() == '*')
                    {
                        bareParam.pop_back();
                        if (!isScalarType(trimCopy(bareParam)))
                        {
                            simpleShape = false;
                        }
                        continue;
                    }
                    if (bareParam.find('*') != std::string::npos)
                    {
                        simpleShape = false;
                    }
                }
                const bool scalarReturn = returnType == "void" || isScalarType(returnType);
                // "能不能直出"不再看"类型文本是否相等"：`bool` 投影成 `int32_t`、枚举同样投成
                // `int32_t`，两者与实现的拼写永远不相等，旧判据把这一整批全判成了桩（`IWindow`
                // 里 8 个 `bool` + 夹具的 `GetBool`/`GetMode`）。真正要排除的只有"镜像结构体"——
                // 它的投影就是在原名后面加一个 `C`。
                const std::string directReturnBare = bareType(method.returnSpelling);
                const bool isDirectReturn = returnType != (directReturnBare + "C");
                // 字符串入参（任务 2.4）：C ABI 面把 `castl::string` / `string_view` 拆成
                // `(char const*, uint64_t)` 两个参数，调用时必须用它们**造一个临时量**再传给实现。
                // 这一支只处理"除字符串外全是标量、且返回 void"的形参集合（`IWindow::SetWindowName`
                // 就是这种）；其它组合仍落桩，会被桩判据抓出来。
                bool stringCallable = true;
                bool hasStringParam = false;
                for (auto const& parameter : method.params)
                {
                    const std::string bare = bareType(parameter.first);
                    const bool stringish = hasPrefix(bare, "castl::string_view") ||
                                           hasPrefix(bare, "std::string_view") ||
                                           bare == "castl::string" || bare == "std::string";
                    hasStringParam = hasStringParam || stringish;
                    if (!stringish && !isScalarType(bare))
                    {
                        stringCallable = false;
                        break;
                    }
                }
                // ---- 统一参数转换描述（任务 2.1 / design 决策一）----
                // 每个形参只在这里生成一次：原生调用前的局部转换（prelude）与实参表达式（nativeArgs）。
                // 普通返回、记录返回、字符串伴生、弱/强/借用句柄返回都消费同一份 —— 返回类型不再决定
                // 参数是否被转换。
                const std::string zeroReturn =
                    returnType == "void" ? std::string("    return;\n") : std::string("    return {};\n");
                std::string nativePrelude;
                std::vector<std::string> nativeArgs;
                bool nativeArgsSupported = true;
                for (std::size_t paramIndex = 0; paramIndex < method.params.size(); ++paramIndex)
                {
                    const std::string spelling = trimCopy(method.params[paramIndex].first);
                    const std::string bare = bareType(spelling);
                    const std::string paramName = method.params[paramIndex].second.empty()
                                                      ? std::string("arg") + std::to_string(paramIndex)
                                                      : method.params[paramIndex].second;
                    // 注入参数（任务 3.3）：C ABI 里没有它，thunk 从固定名钩子取值。
                    if (method.injectedParams.count(method.params[paramIndex].second) > 0)
                    {
                        const std::string hookName =
                            injectHookName(method.ownerQualified, method.name,
                                           method.params[paramIndex].second);
                        // 强转要用**完整限定拼写**：头里的形参拼写常是短名（`TaskScheduler *`），
                        // 它靠 `using namespace` 才成立；生成的 thunk 在全局作用域，短名找不到
                        // （实测 `error C2061: syntax error: identifier 'TaskScheduler'`）。
                        const std::string castSpelling =
                            paramIndex < method.paramCanonicalSpellings.size() &&
                                    !method.paramCanonicalSpellings[paramIndex].empty()
                                ? method.paramCanonicalSpellings[paramIndex]
                                : spelling;
                        nativeArgs.push_back("static_cast<" + castSpelling + ">(" + hookName + "())");
                        continue;
                    }
                    // 内存窗（任务 1.1 / design D2）：指针参数对应 ABI 的 `<名>`；被吸收的长度参数
                    // 由托管算、经 ABI 的 `<名>Count` 进来。
                    {
                        auto windowPointer = method.windowLengthByPointer.find(paramIndex);
                        if (windowPointer != method.windowLengthByPointer.end())
                        {
                            nativeArgs.push_back(paramName);
                            continue;
                        }
                        auto windowLength = method.windowPointerByLength.find(paramIndex);
                        if (windowLength != method.windowPointerByLength.end())
                        {
                            const std::size_t pointerIndex = windowLength->second;
                            const std::string pointerName =
                                method.params[pointerIndex].second.empty()
                                    ? std::string("arg") + std::to_string(pointerIndex)
                                    : method.params[pointerIndex].second;
                            nativeArgs.push_back("static_cast<uint64_t>(" + pointerName + "Count)");
                            continue;
                        }
                    }
                    const std::string canonicalSpelling =
                        paramIndex < method.paramCanonicalSpellings.size()
                            ? method.paramCanonicalSpellings[paramIndex]
                            : spelling;
                    const int paramKind = paramIndex < method.paramKinds.size() ? method.paramKinds[paramIndex] : 0;
                    const bool outRef = spelling.find('&') != std::string::npos &&
                                        spelling.find("const") == std::string::npos;
                    std::string local = paramName;
                    // 普通枚举：ABI 是 int32_t，原生实参显式转回枚举。
                    if (paramKind == static_cast<int>(CXType_Enum))
                    {
                        EnumInfo const* info = findEnumBySpelling(canonicalSpelling);
                        if (info == nullptr)
                        {
                            nativeArgsSupported = false;
                            break;
                        }
                        nativeArgs.push_back("static_cast<" + info->cppName + ">(" +
                                             (outRef ? "*" + paramName : paramName) + ")");
                        continue;
                    }
                    // EnumFlags<T>：ABI 是 MaskType，原生实参显式重建掩码。
                    {
                        std::string flagsInner;
                        if (parseFlagsWrapper(canonicalSpelling, &flagsInner))
                        {
                            EnumInfo const* info = findEnumByShort(flagsInner);
                            if (info == nullptr)
                            {
                                nativeArgsSupported = false;
                                break;
                            }
                            nativeArgs.push_back("::uenum::EnumFlags<" + info->cppName + ">(" + paramName + ")");
                            continue;
                        }
                    }
                    // 资源引用值类（任务 3.1）：按 kind 分派**全部**对象构造通道（ImageHandle 有
                    // GPUTexture → External / WindowHandle → Backbuffer 两条，design D12）与名称通道，
                    // 组装栈上值类再传给原生。只出第一条对象通道会让"按窗口句柄构造的后台缓冲引用"
                    // 在边界上被判无效（Present 永远拿不到 Backbuffer 引用）。
                    {
                        auto valueType = g_valueTypes.find(bare);
                        if (valueType != g_valueTypes.end() && !outRef)
                        {
                            const ValueTypeInfo& valueInfo = valueType->second;
                            const std::string valueLocal = paramName + "Value";
                            nativePrelude += "    " + valueInfo.qualifiedName + " " + valueLocal + ";\n";
                            nativePrelude += "    switch (" + paramName + "Kind)\n    {\n";
                            for (ValueTypeInfo::ObjectChannel const& channel : valueInfo.objectChannels)
                            {
                                nativePrelude += "    case " + std::to_string(channel.kind) +
                                                 "u:\n    {\n";
                                nativePrelude += "        std::shared_ptr<void> " + paramName + "Strong = " +
                                                 linear + "_lookupStrongHandle(self, " + paramName +
                                                 "Handle, \"" + channel.pointeeQualified + "\", \"" +
                                                 paramName + "\");\n";
                                nativePrelude += "        if (!" + paramName + "Strong) { " +
                                                 trimCopy(zeroReturn) + " }\n";
                                nativePrelude += "        " + valueLocal + " = " + valueInfo.qualifiedName +
                                                 "(std::static_pointer_cast<" +
                                                 channel.pointeeQualified + ">(" + paramName +
                                                 "Strong));\n";
                                nativePrelude += "        break;\n    }\n";
                            }
                            if (valueInfo.hasNameCtor)
                            {
                                nativePrelude += "    case " + std::to_string(valueInfo.nameKind) +
                                                 "u:\n    {\n";
                                nativePrelude += "        if (" + paramName + "Name == nullptr || " + paramName +
                                                 "NameLen == 0u)\n        {\n";
                                nativePrelude += "            " + linear + "_setError(self, " + linear +
                                                 "_ERROR_NULL_POINTER, \"空名称不能构造资源引用\");\n";
                                nativePrelude += "            " + trimCopy(zeroReturn) + "\n        }\n";
                                nativePrelude += "        " + valueLocal + " = " + valueInfo.qualifiedName +
                                                 "(cacore::NameHash(castl::string(reinterpret_cast<char const*>(" +
                                                 paramName + "Name), static_cast<std::size_t>(" + paramName +
                                                 "NameLen))));\n";
                                nativePrelude += "        break;\n    }\n";
                            }
                            if (valueInfo.hasInvalidKind)
                            {
                                // 无效态：传默认构造值（原生侧按"无效句柄"处理）。参数位置用它的
                                // 唯一场景是记录字段的镜像值，方法参数本身不该出现无效值 —— 这里
                                // 仍然放行而不猜一个对象通道，让原生实现自己去拒绝空句柄。
                                nativePrelude += "    case " + std::to_string(valueInfo.invalidKind) +
                                                 "u:\n        break;\n";
                            }
                            nativePrelude += "    default:\n        " + linear + "_setError(self, " +
                                             linear +
                                             "_ERROR_NULL_POINTER, \"无效的资源引用类型\");\n        " +
                                             trimCopy(zeroReturn) + "\n    }\n";
                            nativeArgs.push_back(valueLocal);
                            continue;
                        }
                    }
                    // 序列化块记录（任务 3.2）：先反序列化成栈对象，块头/载荷错误各自落具名错误码，
                    // 失败路径不调用原生实现（实现调用计数保持 0）。
                    {
                        auto serialized = g_serializedTypes.find(bare);
                        if (serialized != g_serializedTypes.end() && !outRef)
                        {
                            const SerializedTypeInfo& blockType = serialized->second;
                            const std::string valueLocal = paramName + "Value";
                            // D11：块里可能有值类容器元素 / 接口对象引用，按句柄从绑定对象表反查。
                            // 解析器把 `self` 交给产物自己的 `_lookupStrongHandle`（它负责类型核对、
                            // 查不到时写错误码并返回空 ⇒ 块读取返回 12 ⇒ 边界拒）。
                            nativePrelude += "    cacore::codegen::BindingHandleResolver " + paramName +
                                             "Resolver;\n";
                            nativePrelude += "    " + paramName + "Resolver.user = self;\n";
                            nativePrelude += "    " + paramName +
                                             "Resolver.resolve = [](void* user, uint64_t handle, "
                                             "char const* expectedType, char const* fieldPath) "
                                             "-> std::shared_ptr<void> { return " + linear +
                                             "_lookupStrongHandle(user, handle, expectedType, fieldPath); };\n";
                            nativePrelude += "    " + blockType.qualifiedName + " " + valueLocal + ";\n";
                            nativePrelude += "    {\n";
                            nativePrelude += "        const uint32_t blockCode = cacore::codegen::"
                                             "CodeGenBindingCodec<" + blockType.qualifiedName +
                                             ">::Deserialize(" + valueLocal + ", " + paramName +
                                             "Bytes, " + paramName + "Len, " + paramName + "Resolver);\n";
                            nativePrelude += "        if (blockCode != 0u)\n        {\n";
                            nativePrelude += "            int32_t blockError = " + linear +
                                             "_ERROR_BAD_BLOCK_PAYLOAD;\n";
                            nativePrelude += "            char const* blockText = \"序列化块载荷不完整或字段读取失败\";\n";
                            nativePrelude += "            switch (blockCode)\n            {\n";
                            nativePrelude += "            case 1u: blockError = " + linear +
                                             "_ERROR_BAD_BLOCK_VERSION; blockText = \"序列化块格式版本不符\"; break;\n";
                            nativePrelude += "            case 2u: blockError = " + linear +
                                             "_ERROR_BAD_BLOCK_TYPE; blockText = \"序列化块类型名哈希不符\"; break;\n";
                            nativePrelude += "            case 3u: blockError = " + linear +
                                             "_ERROR_BAD_BLOCK_LAYOUT; blockText = \"序列化块字段布局哈希不符\"; break;\n";
                            nativePrelude += "            case 12u: blockError = " + linear +
                                             "_ERROR_BAD_OBJECT_HANDLE; blockText = \"序列化块里的对象句柄在绑定对象表里查不到\"; break;\n";
                            nativePrelude += "            default: break;\n            }\n";
                            nativePrelude += "            " + linear +
                                             "_setError(self, blockError, blockText);\n";
                            nativePrelude += "            " + trimCopy(zeroReturn) + "\n        }\n    }\n";
                            nativeArgs.push_back(valueLocal);
                            continue;
                        }
                    }
                    // 字节入参：调用前重建 castl::vector<uint8_t>；空指针只允许配零长度。
                    if (isByteVectorSpelling(spelling))
                    {
                        const std::string bytesLocal = paramName + "Bytes";
                        nativePrelude += "    castl::vector<uint8_t> " + bytesLocal + ";\n";
                        nativePrelude += "    if (" + paramName + " == nullptr)\n    {\n";
                        nativePrelude += "        if (" + paramName + "Count != 0u)\n        {\n";
                        nativePrelude += "            " + linear + "_setError(self, " + linear +
                                         "_ERROR_NULL_POINTER, \"空指针配非零字节长度\");\n";
                        nativePrelude += zeroReturn + "        }\n    }\n";
                        nativePrelude += "    else\n    {\n        " + bytesLocal + ".assign(" + paramName + ", " +
                                         paramName + " + " + paramName + "Count);\n    }\n";
                        nativeArgs.push_back(bytesLocal);
                        continue;
                    }
                    // 独立包装值：名称/路径用现有构造函数重建，HashObj 只传所含值。
                    if (isNameHashSpelling(spelling) || isPathHashSpelling(spelling))
                    {
                        const std::string wrapperType = isNameHashSpelling(spelling) ? "cacore::NameHash"
                                                                                     : "cacore::PathHash";
                        const std::string wrapperLocal = paramName + "Cpp";
                        nativePrelude += "    " + wrapperType + " " + wrapperLocal + "(castl::string(" + paramName +
                                         ", " + paramName + "Len));\n";
                        nativeArgs.push_back(wrapperLocal);
                        continue;
                    }
                    {
                        std::string hashInner;
                        if (parseHashObjWrapper(spelling, &hashInner))
                        {
                            const MirrorType* innerMirror = findMirrorByCpp(bareType(hashInner));
                            if (innerMirror == nullptr)
                            {
                                nativeArgsSupported = false;
                                break;
                            }
                            const std::string valueLocal = paramName + "Value";
                            const std::string hashLocal = paramName + "Hash";
                            nativePrelude += "    " + innerMirror->cppName + " " + valueLocal + " = " + linear +
                                             "_fromC_" + innerMirror->csName + "(*" + paramName + ");\n";
                            nativePrelude += "    cacore::HashObj<" + innerMirror->cppName + "> " + hashLocal + "{" +
                                             valueLocal + "};\n";
                            nativeArgs.push_back(hashLocal);
                            continue;
                        }
                    }
                    // 字符串：ABI 是 char const* + uint64_t，原生侧造成 string / string_view。
                    {
                        const bool stringish = hasPrefix(bare, "castl::string_view") ||
                                               hasPrefix(bare, "std::string_view") ||
                                               bare == "castl::string" || bare == "std::string";
                        if (stringish)
                        {
                            const bool wantsString = bare == "castl::string" || bare == "std::string";
                            const std::string stringLocal = paramName + "Text";
                            nativePrelude += std::string("    ") + (wantsString ? "castl::string " : "castl::string_view ") +
                                             stringLocal + "(" + paramName + ", " + paramName + "Len);\n";
                            nativeArgs.push_back(stringLocal);
                            continue;
                        }
                    }
                    // 智能指针入参：按句柄反查强引用并重建目标类型。
                    if (hasPrefix(bare, "castl::shared_ptr") || hasPrefix(bare, "std::shared_ptr") ||
                        hasPrefix(bare, "std::unique_ptr"))
                    {
                        std::string inner = bare;
                        const std::size_t open = inner.find('<');
                        const std::size_t close = inner.rfind('>');
                        if (open == std::string::npos || close == std::string::npos || close <= open)
                        {
                            nativeArgsSupported = false;
                            break;
                        }
                        inner = trimCopy(inner.substr(open + 1, close - open - 1));
                        const std::string innerShort = unqualifiedName(bareType(inner));
                        const std::string knownQualified =
                            knownInterfaceQualified(innerShort, shape.qualifiedName);
                        const std::string targetLiteral = knownQualified;
                        // 原生实参类型必须用完整限定名：主头里可能只有短名（跨头文件目标）。
                        std::string qualifiedInner = inner;
                        if (!knownQualified.empty())
                        {
                            qualifiedInner = knownQualified;
                        }
                        else if (const MirrorType* innerMirror = findMirrorByCpp(bareType(inner)))
                        {
                            qualifiedInner = innerMirror->cppName;
                        }
                        const std::string strongLocal = paramName + "Strong";
                        const std::string pointerLocal = paramName + "Shared";
                        nativePrelude += "    std::shared_ptr<void> " + strongLocal + " = " + linear +
                                         "_lookupStrongHandle(self, " + paramName + "Handle, \"" + targetLiteral +
                                         "\", \"" + paramName + "\");\n";
                        nativePrelude += "    if (!" + strongLocal + ")\n    {\n" + zeroReturn + "    }\n";
                        nativePrelude += "    castl::shared_ptr<" + qualifiedInner + "> " + pointerLocal +
                                         " = std::static_pointer_cast<" + qualifiedInner + ">(" + strongLocal + ");\n";
                        nativeArgs.push_back(pointerLocal);
                        continue;
                    }
                    // 纯数据记录（含嵌套）：逐字段从 C 镜像搭回原类型。
                    if (const MirrorType* paramMirror = findMirrorByCpp(bare))
                    {
                        const std::string recordLocal = paramName + "Cpp";
                        nativePrelude += "    " + paramMirror->cppName + " " + recordLocal + " = " + linear +
                                         "_fromC_" + paramMirror->csName + "(*" + paramName + ");\n";
                        nativeArgs.push_back(recordLocal);
                        continue;
                    }
                    // 记录容器（`castl::vector<T> const&`）：逐元素从 C 镜像搭回。
                    {
                        const std::size_t open = bare.find('<');
                        const std::size_t close = bare.rfind('>');
                        if (hasPrefix(bare, "castl::vector") && open != std::string::npos &&
                            close != std::string::npos && close > open && !outRef)
                        {
                            const std::string element = bareType(trimCopy(bare.substr(open + 1, close - open - 1)));
                            const MirrorType* elementMirror = findMirrorByCpp(element);
                            if (elementMirror == nullptr)
                            {
                                nativeArgsSupported = false;
                                break;
                            }
                            const std::string vectorLocal = paramName + "Values";
                            nativePrelude += "    castl::vector<" + elementMirror->cppName + "> " + vectorLocal + ";\n";
                            nativePrelude += "    " + vectorLocal + ".reserve(static_cast<std::size_t>(" + paramName +
                                             "Count));\n";
                            nativePrelude += "    for (uint64_t index = 0u; index < " + paramName +
                                             "Count; ++index)\n    {\n        " + vectorLocal + ".push_back(" +
                                             linear + "_fromC_" + elementMirror->csName + "(" + paramName +
                                             "[index]));\n    }\n";
                            nativeArgs.push_back(vectorLocal);
                            continue;
                        }
                    }
                    // 出参标量 / 枚举：解引用后传回。
                    if (isScalarType(bare))
                    {
                        if (outRef)
                        {
                            nativeArgs.push_back("*" + paramName);
                        }
                        else if (bare == "bool")
                        {
                            nativeArgs.push_back(paramName + " != 0");
                        }
                        else
                        {
                            nativeArgs.push_back(paramName);
                        }
                        continue;
                    }
                    if (!bare.empty() && (bare.back() == '*' || bare == "void"))
                    {
                        nativeArgs.push_back(paramName);
                        continue;
                    }
                    nativeArgsSupported = false;
                    break;
                }
                // 智能指针模板实参（weak_ptr<T> / shared_ptr<T>）：返回方向的跨头文件目标要用它。
                auto smartPointerInner = [](std::string const& spelling) -> std::string {
                    const std::string bare = bareType(spelling);
                    const std::size_t open = bare.find('<');
                    const std::size_t close = bare.rfind('>');
                    if (open == std::string::npos || close == std::string::npos || close <= open)
                    {
                        return std::string();
                    }
                    return trimCopy(bare.substr(open + 1, close - open - 1));
                };
                // 目标类型的完整限定名（跨头文件）：短名必须在已知清单里，否则返回空串。
                auto targetLiteralOf = [&shape](std::string const& typeSpelling) -> std::string {
                    const std::string shortName = unqualifiedName(bareType(typeSpelling));
                    return knownInterfaceQualified(shortName, shape.qualifiedName);
                };
                if (isCallback)
                {
                    // 安装入口（design D1/D2）：函数指针 + 不透明用户令牌 → 原生 `castl::function`，
                    // 返回内部注册号。函数指针为空 = 清空原生回调（返回 0，不产生注册号）。
                    const CallbackShape& callbackShape = callbackInfo.shape;
                    const std::string originalCallbackType =
                        callbackCppFunctionType(callbackShape, namespacePrefixOf(shape.qualifiedName));
                    const std::string fnName = callbackInfo.paramName + "Fn";
                    const std::string userName = callbackInfo.paramName + "User";
                    const std::string indexText = std::to_string(callbackIndex);
                    std::string lambdaParams;
                    std::string forwardArgs;
                    for (std::size_t argIndex = 0; argIndex < callbackShape.args.size(); ++argIndex)
                    {
                        const CallbackArg& argument = callbackShape.args[argIndex];
                        const std::string argName = "arg" + std::to_string(argIndex);
                        std::string argumentCpp = argument.cppType;
                        const std::string argumentBare = bareType(argumentCpp);
                        if (!isScalarType(argumentBare) && argumentCpp.find("::") == std::string::npos)
                        {
                            argumentCpp = namespacePrefixOf(shape.qualifiedName) + argumentCpp;
                        }
                        lambdaParams += (argIndex == 0 ? "" : ", ") + argumentCpp + " " + argName;
                        std::string forwarded;
                        if (!argument.interfaceName.empty())
                        {
                            // 回调里的接口裸指针只作为不透明地址转给托管桥；借用包装在托管侧做。
                            forwarded = "static_cast<void*>(" + argName + ")";
                        }
                        else if (bareType(argument.cppType) == "bool")
                        {
                            forwarded = argName + " ? 1 : 0";
                        }
                        else
                        {
                            forwarded = argName;
                        }
                        forwardArgs += ", " + forwarded;
                    }
                    thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    thunkDefs += "    if (" + fnName + " == nullptr)\n    {\n";
                    thunkDefs += "        if (" + linear + "_callbackModifyBlocked(self, " + indexText +
                                 ")) { return 0u; }\n";
                    thunkDefs += "        " + linear + "_supersedeCallback(self, " + indexText + ");\n";
                    thunkDefs += "        try { object->" + method.name + "(castl::function<" + originalCallbackType +
                                 ">{}); }\n";
                    thunkDefs += "        catch (...) { " + linear + "_setError(self, " + linear +
                                 "_ERROR_CALLBACK_EXCEPTION, \"原生实现抛出了 C++ 异常\"); return 0u; }\n";
                    // 清空不产生注册号，挂起的回调错误可以在这里安全上报（安装成功的路径不 attach，
                    // 否则 C# 会因为异常而丢掉刚拿到的注册号，保活项与 native 注册就对不上了）。
                    thunkDefs += "        " + linear + "_attachCallbackError(self);\n";
                    thunkDefs += "        return 0u;\n    }\n";
                    thunkDefs += "    if (" + linear + "_callbackModifyBlocked(self, " + indexText +
                                 ")) { return 0u; }\n";
                    thunkDefs += "    const uint64_t registration = " + linear + "_registerCallback(self, " +
                                 indexText + ");\n";
                    thunkDefs += "    if (registration == 0u) { return 0u; }\n";
                    thunkDefs += "    auto callback = [registration, self, " + fnName + ", " + userName + "](" +
                                 lambdaParams + ") {\n";
                    thunkDefs += "        " + linear + "_beginCallback(registration);\n";
                    thunkDefs += "        try { " + fnName + "(" + userName + forwardArgs + "); }\n";
                    thunkDefs += "        catch (std::exception const& error) { " + linear +
                                 "_setCallbackError(self, " + linear +
                                 "_ERROR_CALLBACK_EXCEPTION, error.what()); }\n";
                    thunkDefs += "        catch (...) { " + linear + "_setCallbackError(self, " + linear +
                                 "_ERROR_CALLBACK_EXCEPTION, \"原生回调桥抛出了 C++ 异常\"); }\n";
                    thunkDefs += "        " + linear + "_endCallback(registration);\n    };\n";
                    thunkDefs += "    try { object->" + method.name + "(castl::function<" + originalCallbackType +
                                 ">(callback)); }\n";
                    thunkDefs += "    catch (...) { " + linear + "_unregisterCallback(self, registration); " +
                                 linear + "_setError(self, " + linear +
                                 "_ERROR_CALLBACK_EXCEPTION, \"原生实现抛出了 C++ 异常\"); return 0u; }\n";
                    thunkDefs += "    return registration;\n";
                }
                else if (returnType == "uint64_t" &&
                    (hasPrefix(directReturnBare, "castl::weak_ptr") ||
                     hasPrefix(directReturnBare, "std::weak_ptr")))
                {
                    // 弱句柄：登记进同一张表的弱引用槽；返回对象是已知接口时记录目标类型，
                    // 公开 C# 才能用目标入口解析（任务 3.2）。
                    if (!nativeArgsSupported)
                    {
                        thunkDefs += "    /* 形参里有不支持统一转换的形状 */\n" + zeroReturn;
                    }
                    else
                    {
                        const std::string inner = smartPointerInner(method.returnSpelling);
                        const std::string target = targetLiteralOf(inner);
                        thunkDefs += nativePrelude;
                        thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                        std::string call = "object->" + method.name + "(";
                        for (std::size_t index = 0; index < nativeArgs.size(); ++index)
                        {
                            call += (index == 0 ? "" : ", ") + nativeArgs[index];
                        }
                        call += ")";
                        thunkDefs += "    return " + linear + "_registerWeakHandle(self, std::weak_ptr<void>(" +
                                     call + "), \"" + target + "\");\n";
                    }
                }
                else if (method.returnsSelfRef && nativeArgsSupported)
                {
                    // 返回自身引用（任务 2.2）：C ABI 槽是 void；原生返回的 *this 就是 self，不需要写回。
                    thunkDefs += nativePrelude;
                    thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    std::string call = "object->" + method.name + "(";
                    for (std::size_t index = 0; index < nativeArgs.size(); ++index)
                    {
                        call += (index == 0 ? "" : ", ") + nativeArgs[index];
                    }
                    call += ")";
                    thunkDefs += "    " + call + ";\n";
                }
                else if (stringCallable && returnType == "void" && isDirectReturn && nativeArgsSupported)
                {
                    // 普通 void 调用：字符串 / 记录 / 字节 / 枚举 / 标志位 / 智能指针入参都消费同一份
                    // `nativeArgs`（任务 2.1）。返回类型不再决定参数是否被转换。
                    thunkDefs += nativePrelude;
                    thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    std::string call = "object->" + method.name + "(";
                    for (std::size_t index = 0; index < nativeArgs.size(); ++index)
                    {
                        call += (index == 0 ? "" : ", ") + nativeArgs[index];
                    }
                    call += ")";
                    thunkDefs += "    " + call + ";\n";
                    // 回调错误槽在这里上报（design D5）：用户委托的异常在下一次 UpdateSystem 抛给调用方。
                    if (hasCallbacks && method.name == "UpdateSystem")
                    {
                        thunkDefs += "    " + linear + "_attachCallbackError(self);\n";
                    }
                }
                // 借用句柄（任务 5.3 / design D9）：返回的是另一个可绑定接口的裸指针 ——
                // 只登记裸指针、不持有；见证者是发行它的 `self`（它没了这个借用指针就不再担保）
                else if (isHandleReturn &&
                         g_bindableInterfaceNames.count(unqualifiedName(directReturnBare)) > 0 &&
                         !hasPrefix(directReturnBare, "castl::shared_ptr") &&
                         !hasPrefix(directReturnBare, "std::shared_ptr") &&
                         !hasPrefix(directReturnBare, "std::unique_ptr"))
                {
                    // 借用句柄（任务 5.3 / design D9）：只登记裸指针、不持有；见证者是发行它的 `self`。
                    if (!nativeArgsSupported)
                    {
                        thunkDefs += "    /* 形参里有不支持统一转换的形状 */\n" + zeroReturn;
                    }
                    else
                    {
                        const std::string target = targetLiteralOf(directReturnBare);
                        thunkDefs += nativePrelude;
                        thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                        std::string borrowCall = "static_cast<void*>(object->" + method.name + "(";
                        for (std::size_t index = 0; index < nativeArgs.size(); ++index)
                        {
                            borrowCall += (index == 0 ? "" : ", ") + nativeArgs[index];
                        }
                        borrowCall += "))";
                        thunkDefs += "    return " + linear + "_registerBorrowedHandle(self, " + borrowCall +
                                     ", \"" + target + "\");\n";
                    }
                }
                // 句柄返回：保活 + 登记；句柄参数：反查（失效就置错误码，不崩）
                else if (isHandleReturn)
                {
                    if (!nativeArgsSupported)
                    {
                        thunkDefs += "    /* 形参里有不支持统一转换的形状 */\n" + zeroReturn;
                    }
                    else
                    {
                        const std::string retInner = smartPointerInner(method.returnSpelling);
                        const std::string target = targetLiteralOf(retInner);
                        thunkDefs += nativePrelude;
                        thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                        std::string call = "object->" + method.name + "(";
                        for (std::size_t index = 0; index < nativeArgs.size(); ++index)
                        {
                            call += (index == 0 ? "" : ", ") + nativeArgs[index];
                        }
                        call += ")";
                        thunkDefs += "    auto result = " + call + ";\n";
                        thunkDefs += "    if (!result) { " + linear + "_setError(self, " + linear +
                                     "_ERROR_NULL_POINTER, \"实现返回了空指针\"); return 0u; }\n";
                        thunkDefs += "    return " + linear +
                                     "_registerStrongHandle(self, std::shared_ptr<void>(result), \"" + target +
                                     "\");\n";
                    }
                }
                else if (scalarReturn && isDirectReturn && nativeArgsSupported)
                {
                    thunkDefs += nativePrelude;
                    thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    std::string call = "object->" + method.name + "(";
                    for (std::size_t index = 0; index < nativeArgs.size(); ++index)
                    {
                        call += (index == 0 ? "" : ", ") + nativeArgs[index];
                    }
                    call += ")";
                    if (returnType == "void")
                    {
                        thunkDefs += "    " + call + ";\n";
                        // 回调错误槽在这里上报（design D5）：用户委托的异常在下一次 UpdateSystem 抛给调用方。
                        if (hasCallbacks && method.name == "UpdateSystem")
                        {
                            thunkDefs += "    " + linear + "_attachCallbackError(self);\n";
                        }
                    }
                    else if (directReturnBare == "bool")
                    {
                        // `bool` 投影成 `int32_t`，不能直接 `return true/false`：必须落成 0/1
                        thunkDefs += "    return " + call + " ? 1 : 0;\n";
                    }
                    else if (method.returnKind == static_cast<int>(CXType_Enum))
                    {
                        // `enum class` 不能隐式转 `int32_t`，必须显式转换
                        thunkDefs += "    return static_cast<int32_t>(" + call + ");\n";
                    }
                    else if (returnType == "uint64_t" && directReturnBare == "void")
                    {
                        // `void*` → 不透明令牌：指针显式转成整数（x64 上 `uint64_t` 装得下）
                        thunkDefs += "    return reinterpret_cast<uint64_t>(" + call + ");\n";
                    }
                    else
                    {
                        thunkDefs += "    return " + call + ";\n";
                    }
                }
                // 镜像结构体**按值返回**（矩阵第 2 行，`IBindRow02::GetThing`）：C 侧是 `<T>C`，
                // C++ 侧是 `T` —— thunk 按同一份 nativeArgs 把实参补齐，再把返回值逐字段搬回。
                else if (mirrorReturn != nullptr && nativeArgsSupported)
                {
                    if (!method.isStatic)
                    {
                        thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    }
                    // 显示器编号校验（本 change 任务 3.2 / design D9）：必须在调原生实现**之前**拦下
                    // 负数与越界编号。原生 `GetMonitor` 用 `castl::clamp` 把无效编号折成另一个真实
                    // 显示器，放进去就再也分不清"调用成功"和"被静默改正"。
                    if (!method.isStatic && method.name == "GetMonitor" && !method.params.empty() &&
                        isScalarType(bareType(method.params[0].first)))
                    {
                        const std::string monitorName = method.params[0].second;
                        thunkDefs += "    if (" + monitorName + " < 0 || " + monitorName +
                                     " >= object->GetMonitorCount())\n    {\n";
                        thunkDefs += "        char monitorError[96] = {0};\n";
                        thunkDefs += "        ::snprintf(monitorError, sizeof(monitorError), \"无效的显示器编号 %d（数量 %d）\", " +
                                     monitorName + ", object->GetMonitorCount());\n";
                        thunkDefs += "        " + linear + "_setError(self, " + linear +
                                     "_ERROR_INVALID_MONITOR, monitorError);\n";
                        thunkDefs += "        return {};\n    }\n";
                    }
                    thunkDefs += nativePrelude;
                    std::string call = (method.isStatic ? shape.qualifiedName + "::" + method.name
                                                        : std::string("object->") + method.name) + "(";
                    for (std::size_t index = 0; index < nativeArgs.size(); ++index)
                    {
                        call += (index == 0 ? "" : ", ") + nativeArgs[index];
                    }
                    call += ")";
                    thunkDefs += "    " + qualify(mirrorReturn->cppName) + " result = " + call + ";\n";
                    thunkDefs += "    return " + linear + "_toC_" + mirrorReturn->csName + "(result);\n";
                }
                else
                {
                    // 未映射的组合不应该出现在产物里：projectMethod 已经在写产物前拒绝它。
                    thunkDefs += "    /* 未支持的形状组合（projectMethod 本应拒绝） */\n" + zeroReturn;
                }
                thunkDefs += "}\n\n";

                // 导出入口：`extern "C"` 的那一层只是把参数原样转给 thunk（声明在上面，定义在这里）
                externDefs += "extern \"C\" " + returnType + " " + linear + "__" + abiName +
                              "(void* self";
                for (Projected const& projected : params)
                {
                    externDefs += ", " + projectedDeclaration(projected);
                }
                externDefs += ")\n{\n";
                {
                    std::string callArguments = "self";
                    for (Projected const& projected : params)
                    {
                        callArguments += ", " + projected.name;
                    }
                    if (returnType == "void")
                    {
                        externDefs += "    " + linear + "_thunk_" + abiName + "(" + callArguments +
                                      ");\n";
                    }
                    else
                    {
                        externDefs += "    return " + linear + "_thunk_" + abiName + "(" +
                                      callArguments + ");\n";
                    }
                }
                externDefs += "}\n\n";
            }
            // ---- 构造入口（任务 2.3）：带公开默认构造的非抽象类出 `<类>_Create` 槽 ----
            const bool emitCreate = shape.hasPublicDefaultCtor && !shape.isAbstract;
            if (emitCreate)
            {
                cpp += "extern \"C\" uint64_t " + linear + "__Create(void* self);\n";
                thunkDefs += "static uint64_t " + linear + "_thunk_Create(void* self)\n{\n";
                thunkDefs += "    " + linear + "_clearError(self);\n";
                thunkDefs += "    std::shared_ptr<" + shape.qualifiedName + "> object(new " +
                             shape.qualifiedName + "());\n";
                thunkDefs += "    return " + linear +
                             "_registerStrongHandle(self, std::static_pointer_cast<void>(object), \"" +
                             shape.qualifiedName + "\");\n}\n\n";
                externDefs += "extern \"C\" uint64_t " + linear + "__Create(void* self)\n{\n";
                externDefs += "    return " + linear + "_thunk_Create(self);\n}\n\n";
                tableSlots.push_back({"Create", "uint64_t (*Create)(void* self)",
                                      "&" + linear + "_thunk_Create", "void*, ulong"});
            }
            if (hasCallbacks)
            {
                // 通用注销伴生（design D1）：按注册号清理仍匹配的那一项；旧注册号静默无操作，
                // 不许误删后来替换的新回调。它进函数表 —— 托管侧只有函数表地址，没有导出符号名。
                cpp += "extern \"C\" void " + linear + "__UnregisterCallback(void* self, uint64_t registration);\n";
                thunkDefs += "static void " + linear +
                             "_thunk_UnregisterCallback(void* self, uint64_t registration)\n{\n";
                thunkDefs += "    " + linear + "_clearError(self);\n";
                thunkDefs += "    " + linear + "_unregisterCallback(self, registration);\n";
                thunkDefs += "    " + linear + "_attachCallbackError(self);\n}\n\n";
                externDefs += "extern \"C\" void " + linear +
                              "__UnregisterCallback(void* self, uint64_t registration)\n{\n";
                externDefs += "    " + linear + "_thunk_UnregisterCallback(self, registration);\n}\n\n";
                tableSlots.push_back({"UnregisterCallback",
                                      "void (*UnregisterCallback)(void* self, uint64_t registration)",
                                      "&" + linear + "_thunk_UnregisterCallback", "void*, ulong, void"});
                // 托管桥把回调里捕获的异常 / 跨线程 / 重入错误写进回调错误槽的入口
                cpp += "extern \"C\" void " + linear +
                       "__ReportCallbackError(void* self, int32_t code, char const* text, uint64_t textLen);\n";
                tableSlots.push_back({"ReportCallbackError",
                                      "void (*ReportCallbackError)(void* self, int32_t code, char const* text, "
                                      "uint64_t textLen)",
                                      "&" + linear + "__ReportCallbackError",
                                      "void*, int, byte*, ulong, void"});
            }
            // 目标接口两个槽：**追加**在函数表末尾（任务 3.2）。不重排旧槽、ABI 版本不变。
            tableSlots.push_back({"ResolveTargetHandle",
                                  "void* (*ResolveTargetHandle)(void* self, uint64_t handle)",
                                  "&" + linear + "__ResolveTargetHandle", "void*, ulong, void*"});
            tableSlots.push_back({"ReleaseTargetHandle",
                                  "void (*ReleaseTargetHandle)(void* self, uint64_t handle)",
                                  "&" + linear + "__ReleaseTargetHandle", "void*, ulong, void"});
            cpp += "\n";
            // ---- 版本化函数表 + 取表函数（任务 2.2）----
            // 校验口径：**版本相等且大小相等**才通过（与已落地的 Scripting/Managed/Host.cs 一致）；
            // 不符时一个字节都不写、返回 0。`GetApi` 把调用方的 size 收进来，就是为了"只写它给的那一段"。
            // 布局清单已在本文件开头发出（镜像类型必须先于声明）
            // 版本从 1 升到 2（任务 5.10）：表头在 abi_version / size 之后多两个错误通道槽。
            // 两侧 `GetApi` 的"版本相等且大小相等"双校验因此自然拦住上一版产物 —— 不需要
            // 另加一条兼容判断，旧表拿到新版本号直接返回 0。
            cpp += "typedef struct " + linear + "Api_v2 {\n";
            cpp += "    uint32_t abi_version;\n";
            cpp += "    uint32_t size;\n";
            // 表头之后是四个非方法槽（错误通道两槽 + 句柄解析/释放两槽），再是方法与两段式伴生。
            // 顺序全靠 tableSlots，C++ 的 static_assert 与 C# 的偏移常量都从同一份列表算出来。
            for (TableSlot const& slot : tableSlots)
            {
                cpp += "    " + slot.member + ";\n";
            }
            cpp += "} " + linear + "Api_v2;\n\n";
            // 句柄表与错误通道的**数据**必须定义在 thunk 之前（thunk 里要用），实现体可以放在后面 ——
            // 这里先把类型、两张表与三个 helper 的前置声明吐出来，避免"先用后定义"。
            cpp += "static const uint32_t " + linear + "_HANDLE_INDEX_BITS = " + std::to_string(handleIndexBits) + "u;\n";
            cpp += "static const uint32_t " + linear + "_HANDLE_GEN_BITS = " + std::to_string(handleGenerationBits) + "u;\n";
            // 错误码一律具名（本 change 任务 4.3）：唯一来源是上面的 `kBindErrorCodes`，生成物里不许出现
            // 裸字面量；`check_bindings.py` 的 `ERROR_CODE_TABLE` 与 README 的错误码表跟它逐项对齐。
            for (BindErrorCode const& errorCode : kBindErrorCodes)
            {
                if (!usesSerializedBlocks &&
                    std::strncmp(errorCode.name, "BAD_BLOCK_", 10) == 0)
                {
                    continue;
                }
                if (isReverseOnlyErrorCode(errorCode))
                {
                    continue;
                }
                cpp += "static const int32_t " + linear + "_ERROR_" + errorCode.name + " = " +
                       std::to_string(errorCode.value) + ";\n";
            }
            // 错误状态按 **self（实例）** 分槽（任务 5.10 / design D11）：以前是每接口一份
            // static 全局，`LastErrorCode(void*)` 直接把 self 丢掉，于是两个实例会串台，
            // 而且一次失败之后紧接着一次成功的调用读到的仍是上一次的码（D8 会误抛）。
            // 键用 self 指针：每个模块实例、每个窗口对象的 self 都不同，接口这一维由
            // "每个接口一份生成物"本身保证，所以 `(接口, 实例)` 落下来就是 `(本文件, self)`。
            cpp += "typedef struct " + linear + "ErrorSlot { void const* self; int32_t code; char text[256]; } " +
                   linear + "ErrorSlot;\n";
            // 与句柄表同一口径：挂在永不释放的堆对象上，避开静态析构顺序（实测过崩溃）
            cpp += "static std::vector<" + linear + "ErrorSlot>& " + linear +
                   "_errorSlots = *new std::vector<" + linear + "ErrorSlot>();\n";
            cpp += "static std::recursive_mutex " + linear + "_errorMutex;\n";
            // 句柄表是**进程级唯一一张**（任务 5.11 / design D12 乙）：所有接口共用，表项带接口标签。
            // 为什么不能每接口一张：句柄 = (代际 << 48) | 序号，而每个接口的序号都从 1 开始，两张独立
            // 的表会发出**数字完全相同**的句柄；把 A 表的句柄交给 B 表的释放入口，B 表会命中一个同号
            // 的活项并把它清掉 —— 销毁的是一个无关对象（实测，口径见 tasks.md 5.11）。
            cpp += "// 本接口在共用句柄表里的标签：一个本翻译单元独有的静态对象地址，跨接口不会撞\n";
            cpp += "static int const " + linear + "_HANDLE_TAG = 0;\n";
            // 兼容既有读写点（CoreTests 直接读表项的 kind / strong）：类型换成共用的那个
            cpp += "static std::vector<cascaded_bindings::HandleEntry>& " + linear +
                   "_handles = cascaded_bindings::HandleTable();\n";
            const std::string handleMutexName = "cascaded_bindings::HandleMutex()";
            // 错误通道的转发声明必须排在 thunk 之前（thunk 入口第一句就是 clearError）。
            // 槽的键是 self：错误码只对"这次调用的那个实例"有意义。
            cpp += "static " + linear + "ErrorSlot& " + linear + "_errorSlotLocked(void const* self);\n";
            cpp += "static void " + linear + "_clearError(void const* self);\n";
            cpp += "static void " + linear + "_setError(void const* self, int32_t code, char const* text);\n";
            cpp += "static int32_t " + linear + "_readErrorCode(void* self);\n";
            cpp += "static void " + linear + "_readErrorText(void* self, char* dst, uint64_t cap);\n";
            cpp += "static uint64_t " + linear + "_registerStrongHandle(void const* owner, std::shared_ptr<void> object, char const* targetType);\n";
            cpp += "static uint64_t " + linear + "_registerWeakHandle(void const* owner, std::weak_ptr<void> object, char const* targetType);\n";
            cpp += "static uint64_t " + linear + "_registerBorrowedHandle(void const* issuer, void* object, char const* targetType);\n";
            cpp += "static std::shared_ptr<void> " + linear +
                   "_lookupStrongHandle(void const* owner, uint64_t handle, char const* expectedTarget, char const* roleText);\n";
            cpp += "static void* " + linear + "_lookupHandle(uint64_t handle);\n";
            cpp += "static void* " + linear +
                   "_lookupTargetHandle(void const* owner, uint64_t handle, char const* expectedTarget);\n";
            cpp += "static void " + linear + "_releaseHandle(void const* owner, uint64_t handle);\n";
            cpp += "static void " + linear +
                   "_releaseTargetHandle(void const* owner, uint64_t handle, char const* expectedTarget);\n\n";
            // 回调注册表与桥：它的辅助函数要用到上面声明的 `_setError`，而 thunk 又要用它们 —— 夹在中间。
            cpp += callbackSupport;
            cpp += thunkDefs;
            cpp += externDefs;
            // 表头（abi_version / size）之后每个槽都是 8 字节对齐的函数指针：
            // 偏移 = 表头字节数 + 8 × 槽序号。C# 侧要靠这些常量定位成员，所以同时用 static_assert
            // 把偏移钉死在编译期；两侧的数都从同一份 `tableSlots` 算出来。
            {
                // 偏移 = 8 + 8 × 槽序号：表头只有 abi_version(4) + size(4) 这 8 字节，之后每个槽
                // （四个非方法槽在前、方法与两段式伴生在后）都是 8 字节的函数指针，紧挨着排。
                const unsigned tableHeaderBytes = 8u;
                unsigned memberIndex = 0;
                cs += "        // 函数表布局（与 C++ 侧同源常量；不一致由 C++ 的 static_assert 先兜住）\n";
                cs += "        public const int " + shape.name + "Api_v2_Size = " +
                      std::to_string(tableHeaderBytes) + " + 8 * " + std::to_string(tableSlots.size()) + ";\n";
                for (TableSlot const& slot : tableSlots)
                {
                    // 托管侧的字段声明也从这里出：一个槽一行 `delegate* unmanaged<…>`
                    cs += "        public static delegate* unmanaged<" + slot.csType + "> " + slot.name + ";\n";
                    cpp += "static_assert(offsetof(" + linear + "Api_v2, " + slot.name + ") == " +
                           std::to_string(tableHeaderBytes + 8u * memberIndex) +
                           ", \"函数表成员偏移变了：\" \"" + linear + "." + slot.name + "\");\n";
                    cs += "        public const int " + shape.name + "Api_v2_" + slot.name + " = " +
                          std::to_string(tableHeaderBytes + 8u * memberIndex) + ";\n";
                    ++memberIndex;
                }
                cpp += "\n";
            }
            cpp += "static " + linear + "Api_v2 const " + linear + "_api = {\n";
            cpp += "    2u, (uint32_t)sizeof(" + linear + "Api_v2),\n";
            // 四个非方法槽按 tableSlots 的顺序紧跟在表头后面
            for (TableSlot const& slot : tableSlots)
            {
                cpp += "    " + slot.init + ",\n";
            }
            cpp += "};\n\n";
            cpp += "extern \"C\" uint32_t " + linear + "__GetApi(uint32_t version, uint32_t size, " +
                   linear + "Api_v2* out)\n{\n";
            cpp += "    const uint32_t realSize = (uint32_t)sizeof(" + linear + "Api_v2);\n";
            cpp += "    if (version != 2u || size != realSize || out == nullptr)\n    {\n        return 0u;\n    }\n";
            cpp += "    ::memcpy(out, &" + linear + "_api, realSize);\n";
            cpp += "    return realSize;\n}\n\n";
            // ---- 错误通道 + 句柄表（任务 2.4）----
            // 句柄 = 低 <INDEX> 位序号 + 高 <GEN> 位代际（位数在 2.9 变成参数，这里先用默认 48/16）。
            // `Release` 让代际进位 ⇒ 旧句柄立刻失效；失效时置错误码 1001 并留一条文本。
            // ---- 错误状态：按 self 分槽（任务 5.10 / design D11）----
            // 每个槽一个自述文本（最多 255 字节 + NUL），`text` 里永远留一个 NUL。
            cpp += "static " + linear + "ErrorSlot& " + linear + "_errorSlotLocked(void const* self)\n{\n";
            cpp += "    for (auto& slot : " + linear + "_errorSlots)\n    {\n";
            cpp += "        if (slot.self == self) { return slot; }\n    }\n";
            cpp += "    " + linear + "_errorSlots.push_back({self, 0, {0}});\n";
            cpp += "    return " + linear + "_errorSlots.back();\n}\n";
            cpp += "static void " + linear + "_clearError(void const* self)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + linear + "_errorMutex);\n";
            cpp += "    " + linear + "ErrorSlot& slot = " + linear + "_errorSlotLocked(self);\n";
            cpp += "    slot.code = 0;\n    slot.text[0] = 0;\n}\n";
            cpp += "static void " + linear + "_setError(void const* self, int32_t code, char const* text)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + linear + "_errorMutex);\n";
            cpp += "    " + linear + "ErrorSlot& slot = " + linear + "_errorSlotLocked(self);\n";
            cpp += "    slot.code = code;\n    slot.text[0] = 0;\n";
            cpp += "    if (text != nullptr)\n    {\n";
            // 文本长度必须夹住：旧的 `memcpy(strlen + 1)` 在文本超过 255 字节时写穿整个槽
            cpp += "        const std::size_t room = sizeof(slot.text) - 1u;\n";
            cpp += "        const std::size_t length = (::strlen(text) < room) ? ::strlen(text) : room;\n";
            cpp += "        ::memcpy(slot.text, text, length);\n        slot.text[length] = 0;\n    }\n}\n";
            cpp += "static int32_t " + linear + "_readErrorCode(void* self)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + linear + "_errorMutex);\n";
            cpp += "    return " + linear + "_errorSlotLocked(self).code;\n}\n";
            cpp += "static void " + linear + "_readErrorText(void* self, char* dst, uint64_t cap)\n{\n";
            cpp += "    if (cap == 0u || dst == nullptr) { return; }\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + linear + "_errorMutex);\n";
            cpp += "    const " + linear + "ErrorSlot& slot = " + linear + "_errorSlotLocked(self);\n";
            cpp += "    const std::size_t length = ::strlen(slot.text);\n";
            cpp += "    const std::size_t copied = (length < cap - 1u) ? length : cap - 1u;\n";
            cpp += "    ::memcpy(dst, slot.text, copied);\n    dst[copied] = 0;\n}\n";
            // 旧的独立入口保留（既有 CoreTests 与 C ABI 用户还在调），但**必须转发到同一份状态**：
            // 否则"从这里读不到错误、只有表里读得到"会把同一次失败读成两个数。
            cpp += "extern \"C\" int32_t " + linear + "__LastErrorCode(void* self)\n{\n    return " +
                   linear + "_readErrorCode(self);\n}\n";
            cpp += "extern \"C\" int32_t " + linear + "__LastErrorText(void* self, char* dst, uint64_t cap)\n{\n";
            cpp += "    " + linear + "_readErrorText(self, dst, cap);\n";
            cpp += "    return (dst != nullptr && cap > 0u) ? (int32_t)::strlen(dst) : 0;\n}\n\n";
            // ---- 句柄表的实现（所有接口共用一张表，任务 5.11 / design D12 乙）----
            cpp += "static cascaded_bindings::HandleEntry* " + linear + "_claimSlotLocked()\n{\n";
            cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    uint64_t index = 1u;\n";
            cpp += "    while (index <= indexMask && index < " + linear + "_handles.size() + 1u)\n    {\n";
            cpp += "        if (" + linear + "_handles[index - 1u].kind == 0u) { break; }\n        ++index;\n    }\n";
            cpp += "    if (index > indexMask) { return nullptr; }\n";
            cpp += "    if (index > " + linear + "_handles.size()) { " + linear + "_handles.push_back({}); }\n";
            cpp += "    return &" + linear + "_handles[index - 1u];\n}\n";
            cpp += "static uint64_t " + linear +
                   "_handleOfLocked(cascaded_bindings::HandleEntry const* entry)\n{\n";
            cpp += "    const uint64_t index = static_cast<uint64_t>(entry - " + linear +
                   "_handles.data()) + 1u;\n";
            cpp += "    return (static_cast<uint64_t>(entry->generation) << " + linear +
                   "_HANDLE_INDEX_BITS) | index;\n}\n";
            cpp += "static uint64_t " + linear + "_registerStrongHandle(void const* owner, std::shared_ptr<void> object, char const* targetType)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    if (!object) { " + linear + "_setError(owner, " + linear + "_ERROR_NULL_POINTER, \"实现返回了空指针\"); return 0u; }\n";
            cpp += "    cascaded_bindings::HandleEntry* entry = " + linear + "_claimSlotLocked();\n";
            cpp += "    if (entry == nullptr) { return 0u; }\n";
            cpp += "    entry->object = object.get();\n    entry->kind = 1u;\n    entry->hasWitness = false;\n";
            cpp += "    entry->witnessHandle = 0u;\n";
            cpp += "    entry->interfaceTag = &" + linear + "_HANDLE_TAG;\n";
            cpp += "    entry->issuerType = \"" + shape.qualifiedName + "\";\n";
            cpp += "    entry->targetType = targetType;\n";
            cpp += "    entry->strong = object;\n    entry->weak.reset();\n";
            cpp += "    return " + linear + "_handleOfLocked(entry);\n}\n";
            cpp += "static uint64_t " + linear + "_registerWeakHandle(void const* owner, std::weak_ptr<void> object, char const* targetType)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    if (object.expired()) { " + linear + "_setError(owner, " + linear + "_ERROR_NULL_POINTER, \"实现返回了空指针\"); return 0u; }\n";
            cpp += "    cascaded_bindings::HandleEntry* entry = " + linear + "_claimSlotLocked();\n";
            cpp += "    if (entry == nullptr) { return 0u; }\n";
            // 弱句柄不持有强引用，所以 object 先写 weak 换出来的指针；对象一死 lookup 就失败
            cpp += "    entry->object = object.lock().get();\n    entry->kind = 2u;\n    entry->hasWitness = false;\n";
            cpp += "    entry->witnessHandle = 0u;\n";
            cpp += "    entry->interfaceTag = &" + linear + "_HANDLE_TAG;\n";
            cpp += "    entry->issuerType = \"" + shape.qualifiedName + "\";\n";
            cpp += "    entry->targetType = targetType;\n";
            cpp += "    entry->weak = object;\n    entry->strong.reset();\n";
            cpp += "    return " + linear + "_handleOfLocked(entry);\n}\n";
            // 借用句柄（任务 5.3 / design D9）：只存裸指针、不持有；发行者见证 = 发行者在表里那个
            // **活着的表项**（按句柄与代际判，而不是按对象弱引用 —— 别的持有者会让弱引用一直不过期，
            // 于是"见证过期"永远判不出来）。
            cpp += "static bool " + linear + "_witnessAliveLocked(uint64_t witnessHandle)\n{\n";
            cpp += "    if (witnessHandle == 0u) { return false; }\n";
            cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = witnessHandle & indexMask;\n";
            cpp += "    const uint64_t generation = witnessHandle >> " + linear + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + linear + "_handles.size()) { return false; }\n";
            cpp += "    auto const& entry = " + linear + "_handles[index - 1u];\n";
            cpp += "    return entry.kind != 0u && entry.kind != 3u && entry.generation == generation;\n}\n";
            cpp += "static uint64_t " + linear + "_registerBorrowedHandle(void const* issuer, void* object, char const* targetType)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    if (object == nullptr) { " + linear + "_setError(issuer, " + linear + "_ERROR_NULL_POINTER, \"实现返回了空指针\"); return 0u; }\n";
            cpp += "    cascaded_bindings::HandleEntry* entry = " + linear + "_claimSlotLocked();\n";
            cpp += "    if (entry == nullptr) { return 0u; }\n";
            cpp += "    entry->object = object;\n    entry->kind = 4u;\n    entry->hasWitness = false;\n";
            cpp += "    entry->witnessHandle = 0u;\n";
            cpp += "    entry->interfaceTag = &" + linear + "_HANDLE_TAG;\n";
            cpp += "    entry->issuerType = \"" + shape.qualifiedName + "\";\n";
            cpp += "    entry->targetType = targetType;\n";
            cpp += "    entry->strong.reset();\n    entry->weak.reset();\n";
            cpp += "    for (std::size_t slot = 0; slot < " + linear + "_handles.size(); ++slot)\n    {\n";
            cpp += "        auto const& other = " + linear + "_handles[slot];\n";
            cpp += "        if (other.object != issuer || other.kind == 0u || other.kind == 3u) { continue; }\n";
            cpp += "        entry->hasWitness = true;\n";
            cpp += "        entry->witnessHandle = (static_cast<uint64_t>(other.generation) << " + linear +
                   "_HANDLE_INDEX_BITS) | (static_cast<uint64_t>(slot) + 1u);\n";
            cpp += "        break;\n    }\n";
            cpp += "    return " + linear + "_handleOfLocked(entry);\n}\n";
            cpp += "static std::shared_ptr<void> " + linear +
                   "_lookupStrongHandle(void const* owner, uint64_t handle, char const* expectedTarget, char const* roleText)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            cpp += "    const uint64_t generation = handle >> " + linear + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + linear + "_handles.size())\n    {\n";
            cpp += "        " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, roleText);\n        return {};\n    }\n";
            cpp += "    auto& entry = " + linear + "_handles[index - 1u];\n";
            cpp += "    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation)\n    {\n";
            cpp += "        " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, roleText);\n        return {};\n    }\n";
            cpp += "    if (expectedTarget != nullptr && expectedTarget[0] != 0 &&\n";
            cpp += "        (entry.targetType == nullptr || ::strcmp(entry.targetType, expectedTarget) != 0))\n    {\n";
            cpp += "        " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"句柄的目标类型不符\");\n        return {};\n    }\n";
            cpp += "    if (entry.kind == 4u)\n    {\n";
            cpp += "        " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"借用句柄没有可持有的智能指针所有权\");\n        return {};\n    }\n";
            cpp += "    if (entry.kind == 2u)\n    {\n";
            cpp += "        std::shared_ptr<void> locked = entry.weak.lock();\n";
            cpp += "        if (!locked) { " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, roleText); }\n";
            cpp += "        return locked;\n    }\n";
            cpp += "    return entry.strong;\n}\n";
            cpp += "static void* " + linear + "_lookupHandle(uint64_t handle)\n{\n";
            cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            cpp += "    const uint64_t generation = handle >> " + linear + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + linear + "_handles.size()) { return nullptr; }\n";
            cpp += "    auto const& entry = " + linear + "_handles[index - 1u];\n";
            cpp += "    /* kind 3 = 已退休的槽：代际回绕过，永不再分配 */\n    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation) { return nullptr; }\n";
            // 标签校验（任务 5.11）：数字相同、但属于别的接口的句柄，**不许**在这里命中
            cpp += "    if (entry.interfaceTag != &" + linear + "_HANDLE_TAG) { return nullptr; }\n";
            // 弱句柄：每次取用都 lock 一次，对象已析构就判失效。
            // 这里**不写错误码**：错误槽按 self 分，而查表函数不知道调用方是谁 ——
            // 由两个入口（`ResolveHandle` / `releaseHandle`）各自按自己的 self 置码。
            cpp += "    if (entry.kind == 2u)\n    {\n";
            cpp += "        auto locked = entry.weak.lock();\n";
            cpp += "        if (!locked) { return nullptr; }\n";
            cpp += "        return locked.get();\n    }\n";
            // 借用句柄（旧发行者路径）：有见证就按见证判；没有见证仍允许显式释放（兼容行为）。
            cpp += "    if (entry.kind == 4u)\n    {\n";
            cpp += "        if (entry.hasWitness && !" + linear +
                   "_witnessAliveLocked(entry.witnessHandle)) { return nullptr; }\n";
            cpp += "        return const_cast<void*>(entry.object);\n    }\n";
            cpp += "    return const_cast<void*>(entry.object);\n}\n";
            cpp += "static void* " + linear +
                   "_lookupTargetHandle(void const* owner, uint64_t handle, char const* expectedTarget)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            cpp += "    const uint64_t generation = handle >> " + linear + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + linear + "_handles.size())\n    {\n";
            cpp += "        " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"句柄已失效\");\n        return nullptr;\n    }\n";
            cpp += "    auto& entry = " + linear + "_handles[index - 1u];\n";
            cpp += "    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation)\n    {\n";
            cpp += "        " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"句柄已失效\");\n        return nullptr;\n    }\n";
            cpp += "    if (entry.targetType == nullptr || ::strcmp(entry.targetType, expectedTarget) != 0)\n    {\n";
            cpp += "        " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"句柄的目标类型不符\");\n        return nullptr;\n    }\n";
            cpp += "    if (entry.kind == 2u)\n    {\n";
            cpp += "        std::shared_ptr<void> locked = entry.weak.lock();\n";
            cpp += "        if (!locked) { " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"目标对象已死亡\"); return nullptr; }\n";
            cpp += "        return locked.get();\n    }\n";
            cpp += "    if (entry.kind == 4u)\n    {\n";
            cpp += "        if (!entry.hasWitness || !" + linear + "_witnessAliveLocked(entry.witnessHandle))\n        {\n";
            cpp += "            " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"借用句柄的发行者见证不存在或已过期\");\n            return nullptr;\n        }\n";
            cpp += "        return const_cast<void*>(entry.object);\n    }\n";
            cpp += "    return const_cast<void*>(entry.object);\n}\n";
            const std::string releaseEntryCode =
                "    entry.kind = 0u;\n    entry.object = nullptr;\n"
                "    entry.strong.reset();\n    entry.weak.reset();\n"
                "    entry.hasWitness = false;\n    entry.witnessHandle = 0u;\n"
                "    entry.issuerType = nullptr;\n    entry.targetType = nullptr;\n"
                "    const uint32_t nextGeneration = (entry.generation + 1u) & ((1u << " + linear +
                "_HANDLE_GEN_BITS) - 1u);\n"
                "    if (nextGeneration == 0u) { entry.kind = 3u; }\n"
                "    entry.generation = nextGeneration;\n";
            // 释放 = 表项清空 + 强/弱引用一起放掉 + 代际进位（旧句柄因此被判 stale）
            cpp += "static void " + linear + "_releaseHandle(void const* owner, uint64_t handle)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            // 释放走一次 lookup：标签不符 / 已失效都在这里被挡下来，错误码归调用方那个实例
            cpp += "    if (" + linear + "_lookupHandle(handle) == nullptr)\n    {\n        " + linear +
                   "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"stale handle\");\n        return;\n    }\n";
            cpp += "    auto& entry = " + linear + "_handles[index - 1u];\n";
            cpp += releaseEntryCode + "}\n\n";
            // 目标释放：先按序号、代际和目标类型校验，再清表项并进位。目标已死亡或见证过期**不**
            // 阻止清理 —— 这条路径必须能把残槽收掉。
            cpp += "static void " + linear +
                   "_releaseTargetHandle(void const* owner, uint64_t handle, char const* expectedTarget)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    const uint64_t indexMask = (1ull << " + linear + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            cpp += "    const uint64_t generation = handle >> " + linear + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + linear + "_handles.size())\n    {\n";
            cpp += "        " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"句柄已失效\");\n        return;\n    }\n";
            cpp += "    auto& entry = " + linear + "_handles[index - 1u];\n";
            cpp += "    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation)\n    {\n";
            cpp += "        " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"句柄已失效\");\n        return;\n    }\n";
            cpp += "    if (entry.targetType == nullptr || ::strcmp(entry.targetType, expectedTarget) != 0)\n    {\n";
            cpp += "        " + linear + "_setError(owner, " + linear + "_ERROR_STALE_HANDLE, \"句柄的目标类型不符\");\n        return;\n    }\n";
            cpp += releaseEntryCode + "}\n\n";
            // 每接口两个泛用入口（任务 1.3）：`ResolveHandle` 是必需的 —— C ABI 面所有方法入口
            // 的第一个参数都是 `void* self`，没有任何一个收定宽句柄；没有它，C# 拿到句柄之后
            // 没有任何办法用它调方法，"旧句柄报 1001" 这条验收也写不出来。
            // 任务 5.10：`ResolveHandle` 也收 `self` —— 它是最常失败的那个入口，不给自己就没法
            // 把 1001 记到"哪个实例失败了"上（错误槽按 self 分）。
            cpp += "extern \"C\" void " + linear + "__ReleaseHandle(void* self, uint64_t handle)\n{\n";
            cpp += "    " + linear + "_clearError(self);\n";
            cpp += "    " + linear + "_releaseHandle(self, handle);\n}\n\n";
            cpp += "extern \"C\" void* " + linear + "__ResolveHandle(void* self, uint64_t handle)\n{\n";
            cpp += "    " + linear + "_clearError(self);\n";
            // 任务 5.1（`csharp-visible-window`）：解析这条路原来**裸读**句柄表 —— 登记时会 push_back
            // 扩容、释放时清项，两边都持 HandleMutex，只有这里不持，于是并发用例偶发"取不到句柄"
            // （实测 353 次里 9 次红）。三路共用同一把锁，读表也在锁内。
            cpp += "    std::lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex());\n";
            cpp += "    void* object = " + linear + "_lookupHandle(handle);\n";
            cpp += "    if (object == nullptr) { " + linear + "_setError(self, " + linear +
                   "_ERROR_STALE_HANDLE, \"stale handle\"); }\n";
            cpp += "    return object;\n}\n\n";
            // 目标接口入口（任务 3.2）：按目标对象的完整限定类型解析/释放；只追加，不重排旧槽。
            cpp += "extern \"C\" void* " + linear + "__ResolveTargetHandle(void* self, uint64_t handle)\n{\n";
            cpp += "    " + linear + "_clearError(self);\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex());\n";
            cpp += "    return " + linear + "_lookupTargetHandle(self, handle, \"" + shape.qualifiedName +
                   "\");\n}\n\n";
            cpp += "extern \"C\" void " + linear + "__ReleaseTargetHandle(void* self, uint64_t handle)\n{\n";
            cpp += "    " + linear + "_clearError(self);\n";
            cpp += "    " + linear + "_releaseTargetHandle(self, handle, \"" + shape.qualifiedName +
                   "\");\n}\n\n";
            cpp += lateDefs;
            // ================= 托管表面（任务 5.2 / 5.6）=================
            // 三层：
            //   `<I>Bindings`（internal）—— 函数表 + 偏移常量 + Bind + ThrowIfFailed；
            //   `<I>Lease`（internal）—— 一个表面对象背后的 native 资源（self + 句柄 + 发行者的释放入口）；
            //   `<I>`（public sealed）—— 只镜像原声明的签名，方法体一行转给 `<I>Calls`。
            // 指针、句柄、delegate* 一律不出现在 public 类型里（验收②）：公开类里连 `void*` 的字样都没有。
            {
                const std::string bindingsName = shape.name + "Bindings";
                const std::string leaseName = shape.name + "Lease";
                const std::string callsName = shape.name + "Calls";

                auto toSurface = [](std::string const& bare, bool pointer) -> std::string
                {
                    if (bare == "void")
                    {
                        return pointer ? "IntPtr" : "void";
                    }
                    if (bare == "bool")
                    {
                        return "bool";
                    }
                    if (bare == "float")
                    {
                        return "float";
                    }
                    if (bare == "double")
                    {
                        return "double";
                    }
                    if (bare == "int" || bare == "int32_t")
                    {
                        return "int";
                    }
                    if (bare == "uint32_t" || bare == "unsigned" || bare == "unsigned int")
                    {
                        return "uint";
                    }
                    if (bare == "int64_t" || bare == "long")
                    {
                        return "long";
                    }
                    if (bare == "uint64_t" || bare == "unsigned long")
                    {
                        return "ulong";
                    }
                    if (bare == "uint16_t" || bare == "unsigned short")
                    {
                        return "ushort";
                    }
                    if (bare == "int16_t" || bare == "short")
                    {
                        return "short";
                    }
                    if (bare == "uint8_t" || bare == "unsigned char")
                    {
                        return "byte";
                    }
                    if (bare == "int8_t" || bare == "signed char" || bare == "char")
                    {
                        return "sbyte";
                    }
                    if (bare == "size_t")
                    {
                        return "ulong";
                    }
                    if (hasPrefix(bare, "castl::string_view") || hasPrefix(bare, "std::string_view") ||
                        bare == "castl::string" || bare == "std::string")
                    {
                        return "string";
                    }
                    // 其余就是已绑定接口的名字（借用/弱引用返回） —— 表面用同名类型，逐字照抄
                    // 认不出来的类型返回空串，调用方会**跳过这一条并打印原因**（不许静默降级）
                    return std::string();
                };
                auto innerOfWeak = [](std::string const& bare) -> std::string
                {
                    for (char const* prefix : {"castl::weak_ptr<", "std::weak_ptr<", "castl::shared_ptr<",
                                               "std::shared_ptr<", "std::unique_ptr<"})
                    {
                        if (hasPrefix(bare, prefix))
                        {
                            std::string inner = bare.substr(std::strlen(prefix));
                            if (!inner.empty() && inner.back() == '>')
                            {
                                inner.pop_back();
                            }
                            return trimCopy(inner);
                        }
                    }
                    return std::string();
                };
                auto capitalize = [](std::string const& text) -> std::string
                {
                    if (text.empty())
                    {
                        return text;
                    }
                    std::string out = text;
                    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
                    return out;
                };

                // ---- 表绑定 + 错误检查（仍在 <-I>Bindings 里）----
                cs += "        public static void Bind(byte* table)\n        {\n";
                {
                    unsigned slotIndex = 0;
                    for (TableSlot const& slot : tableSlots)
                    {
                        cs += "            " + slot.name + " = (delegate* unmanaged<" + slot.csType +
                              ">)*(void**)(table + " + std::to_string(8u + 8u * slotIndex) + ");\n";
                        ++slotIndex;
                    }
                }
                cs += "        }\n";
                cs += "        public static void ThrowIfFailed(void* self)\n        {\n";
                cs += "            if (LastErrorCode == null) { return; }\n";
                cs += "            int code = LastErrorCode(self);\n";
                cs += "            if (code == 0) { return; }\n";
                cs += "            throw new " + shape.name + "Exception(code, ReadError(self));\n        }\n";
                cs += "        private static string ReadError(void* self)\n        {\n";
                cs += "            if (LastErrorText == null) { return string.Empty; }\n";
                cs += "            byte* buffer = stackalloc byte[256];\n";
                cs += "            LastErrorText(self, buffer, 256UL);\n";
                cs += "            return System.Runtime.InteropServices.Marshal.PtrToStringUTF8((IntPtr)buffer) ?? string.Empty;\n";
                cs += "        }\n";
                // 错误标识在 C# 侧也具名（本 change 任务 4.3）：桥与表面代码不许手抄数字。
                for (BindErrorCode const& errorCode : kBindErrorCodes)
                {
                    if (!usesSerializedBlocks &&
                        std::strncmp(errorCode.name, "BAD_BLOCK_", 10) == 0)
                    {
                        continue;
                    }
                    if (isReverseOnlyErrorCode(errorCode))
                    {
                        continue;
                    }
                    cs += "        public const int ERROR_" + std::string(errorCode.name) + " = " +
                          std::to_string(errorCode.value) + ";\n";
                }
                if (hasCallbacks)
                {
                    // 托管桥报告回调错误的唯一入口：把 UTF-8 文本钉住再交给 native 的错误槽。
                    cs += "        public static void ReportCallbackErrorText(void* self, int code, string message)\n        {\n";
                    cs += "            byte[] bytes = System.Text.Encoding.UTF8.GetBytes(message);\n";
                    cs += "            fixed (byte* pointer = bytes) { ReportCallbackError(self, code, pointer, (ulong)bytes.Length); }\n";
                    cs += "        }\n";
                }

                // ---- 每个方法的调用壳（managed 签名，指针全留在这层）----
                cs += "    }\n\n"; // 关掉 <-I>Bindings：以下三个类型都是**平级**的 internal/public 类型
                // ---- 异常类型 ----
                cs += "    /// <summary>" + shape.name + " 的失败：Code 是 C ABI 的错误码</summary>\n";
                cs += "    public sealed class " + shape.name + "Exception : Exception\n    {\n";
                cs += "        public " + shape.name +
                      "Exception(int code, string message) : base(message + \"（错误码 \" + code + \"）\")\n";
                cs += "        {\n            Code = code;\n        }\n";
                cs += "        public int Code { get; }\n    }\n\n";
                if (isBorrowable)
                {
                    // 回调期间借用的表面对象共享这个作用域：原生回调返回后 Active=false，
                    // 保存下来的对象再调用任意方法都会先撞上 `<接口>Calls` 的失效检查（1001）。
                    cs += "    /// <summary>回调借用作用域：返回后 Active=false，借用对象立即失效</summary>\n";
                    cs += "    internal sealed class " + shape.name + "BorrowScope\n    {\n";
                    cs += "        public bool Active = true;\n    }\n\n";
                }

                // ---- 资源租约（internal：native 细节都在这层）----
                cs += "    /// <summary>表面对象背后的 native 资源：对象指针 + 句柄 + 发行它的那个接口的释放入口</summary>\n";
                cs += "    internal sealed unsafe class " + leaseName + "\n    {\n";
                cs += "        public void* Self;\n        public ulong Handle;\n        public void* ReleaseSelf;\n";
                if (isBorrowable)
                {
                    // 回调里借用的表面对象：租约带作用域，回调返回后统一失效（design D3）。
                    cs += "        public " + shape.name + "BorrowScope Scope;\n";
                }
                cs += "        public delegate* unmanaged<void*, ulong, void> Release;\n";
                cs += "        public bool Disposed;\n";
                cs += "        public void Dispose()\n        {\n";
                cs += "            if (Disposed) { return; }\n";
                // 公开租约释放必须检查错误通道（任务 3.3）：把合法释放错接成发行者入口时，
                // 原生侧会写下 1001，这里要真的抛出来，而不是静默清字段。
                cs += "            if (Handle != 0UL && Release != null)\n            {\n";
                cs += "                Release(ReleaseSelf, Handle);\n";
                cs += "                " + bindingsName + ".ThrowIfFailed(ReleaseSelf);\n";
                cs += "            }\n";
                cs += "            Handle = 0UL;\n            Self = null;\n            ReleaseSelf = null;\n";
                cs += "            Release = null;\n            Disposed = true;\n        }\n    }\n\n";
                cs += "    /// <summary>表面方法的真正调用体：指针、句柄、string 的编解码都在这层（internal）</summary>\n";
                cs += "    internal static unsafe class " + callsName + "\n    {\n";
                // 表面对象把自己的 native self 交给同一程序集里的诊断代码（`IntPtr` 在 public 类型里是允许的）。
                cs += "        internal static IntPtr NativeSelfOf(" + leaseName + " lease) => (IntPtr)lease.Self;\n";
                if (emitCreate)
                {
                    // 构造入口（任务 2.3）：指针与句柄只在这层出现，公开表面只调这个静态方法。
                    cs += "        internal static " + leaseName + " Create()\n        {\n";
                    cs += "            ulong handle = " + bindingsName + ".Create(null);\n";
                    cs += "            " + bindingsName + ".ThrowIfFailed(null);\n";
                    cs += "            if (handle == 0UL) { throw new " + shape.name + "Exception(" + bindingsName +
                          ".ERROR_NULL_POINTER, \"Create 没有返回句柄\"); }\n";
                    cs += "            void* self = " + bindingsName + ".ResolveHandle(null, handle);\n";
                    cs += "            " + bindingsName + ".ThrowIfFailed(null);\n";
                    cs += "            return new " + leaseName + " { Self = self, Handle = handle, "
                          "ReleaseSelf = null, Release = " + bindingsName + ".ReleaseHandle };\n";
                    cs += "        }\n";
                }
                // 布局镜像的逐字段搬运（嵌套记录递归展开）只在这层出现，公开记录本身不带 native 细节。
                emitMirrorCSharpConversions(&cs, shape.name, mirrors);
                if (hasCallbacks)
                {
                    // 表面对象的 Dispose 委托到这里：注销仍存活的注册并释放保活（指针只在这层出现）。
                    cs += "        internal static void DisposeCallbacks(" + leaseName + " lease, " + shape.name +
                          "CallbackSlot[] slots)\n        {\n";
                    cs += "            for (int index = 0; index < slots.Length; ++index)\n            {\n";
                    cs += "                if (slots[index].Registration != 0UL)\n                {\n";
                    cs += "                    " + shape.name + "Bindings.UnregisterCallback(lease.Self, "
                          "slots[index].Registration);\n";
                    cs += "                    slots[index].Registration = 0UL;\n";
                    cs += "                }\n";
                    cs += "                slots[index].Release();\n";
                    cs += "            }\n        }\n";
                }
                std::string surfaceClass;
                surfaceClass += "    public sealed class " + shape.name + " : IDisposable\n    {\n";
                surfaceClass += "        internal " + shape.name + "(" + leaseName + " lease) { m_lease = lease; }\n";
                surfaceClass += "        private readonly " + leaseName + " m_lease;\n";
                // 回调期间借用的窗口对象需要把自己的 native 地址写进日志（两侧按指针值比身份）。
                // 这是 `internal` 成员，公开表面仍然不出现指针 / 句柄；解引用留在 internal 的 Calls 层。
                surfaceClass += "        internal IntPtr NativeSelf => " + callsName + ".NativeSelfOf(m_lease);\n";
                // 智能指针入参要从目标表面对象取回句柄（internal，不出现在公开签名里）；
                // 已释放对象在 Calls 层被挡下，不得进入原生实现。
                surfaceClass += "        internal IntPtr Token => (IntPtr)m_lease.Handle;\n";
                surfaceClass += "        internal bool Disposed => m_lease.Disposed;\n";
                if (emitCreate)
                {
                    // 构造入口（任务 2.3）：native 侧分配并登记句柄；表面对象持同一个租约。
                    surfaceClass += "        public static " + shape.name + " Create()\n        {\n";
                    surfaceClass += "            return new " + shape.name + "(" + callsName + ".Create());\n";
                    surfaceClass += "        }\n";
                }
                if (hasCallbacks)
                {
                    // 回调注册由**表面对象**持有（design D2）：释放表面对象 = 逐项注销它仍持有的回调。
                    std::string slots;
                    for (std::size_t slotIndex = 0; slotIndex < callbackMethods.size(); ++slotIndex)
                    {
                        slots += (slotIndex == 0 ? "" : ", ") + std::string("new ") + shape.name + "CallbackSlot()";
                    }
                    surfaceClass += "        private readonly " + shape.name + "CallbackSlot[] m_callbackSlots = new " +
                                    shape.name + "CallbackSlot[] { " + slots + " };\n";
                    surfaceClass += "        /// <summary>释放回调注册与句柄：注销仍存活的注册后释放表面对象</summary>\n";
                    surfaceClass += "        public void Dispose()\n        {\n";
                    // 指针（`lease.Self`）只在 internal 的 Calls 层出现：public 类型体里连 `void*` 的字样都不许有。
                    surfaceClass += "            " + shape.name + "Calls.DisposeCallbacks(m_lease, m_callbackSlots);\n";
                    surfaceClass += "            m_lease.Dispose();\n        }\n";
                }
                else
                {
                    surfaceClass += "        /// <summary>释放这个对象持有的句柄（对应的 native 对象由实现决定是否销毁）</summary>\n";
                    surfaceClass += "        public void Dispose() { m_lease.Dispose(); }\n";
                }

                for (SurfaceMethod const& surface : surfaceMethods)
                {
                    MethodSignature const& method = *surface.method;
                    const std::string abiName = method.abiName;
                    std::size_t callbackIndex = static_cast<std::size_t>(-1);
                    if (surface.isCallback)
                    {
                        for (CallbackMethod const& candidate : callbackMethods)
                        {
                            if (candidate.name == method.name)
                            {
                                callbackIndex = candidate.index;
                                break;
                            }
                        }
                    }
                    const std::string bare = bareType(method.returnSpelling);
                    const bool returnPointer = method.returnSpelling.find('*') != std::string::npos;
                    const bool stringTwoStep = surface.needsTwoStep &&
                                               (hasPrefix(bare, "castl::string_view") ||
                                                hasPrefix(bare, "std::string_view") || bare == "castl::string" ||
                                                bare == "std::string");
                    // 记录两段式（D8）：托管得记录的序列化只读副本，空指针 ⇒ null。
                    const bool recordTwoStep = !surface.twoStepRecordShort.empty();
                    // 标量容器两段式（本批 1.6 / D13）：托管得元素数组。
                    const bool containerTwoStep = !surface.twoStepScalarElement.empty();
                    const std::string weakInner = innerOfWeak(bare);
                    // 返回的是"另一个接口的对象"吗？弱引用（`weak_ptr<T>`）与借用裸指针（`T*`）都算，
                    // 表面给同名接口对象；夹具里指向非接口类型的句柄不算（下面会跳过并打印原因）。
                    std::string surfaceObjectName;
                    if (!weakInner.empty())
                    {
                        surfaceObjectName = unqualifiedName(weakInner);
                    }
                    else if (surface.isHandleReturn && returnPointer && bare != "void" && !bare.empty())
                    {
                        surfaceObjectName = unqualifiedName(bare);
                    }
                    if (!surfaceObjectName.empty() &&
                        g_bindableInterfaceNames.count(surfaceObjectName) == 0)
                    {
                        surfaceObjectName.clear();
                    }
                    // 返回类型：自返回 → 同名表面类型（`return this`）；两段式字符串 → string；
                    // 弱/借用句柄 → 同名接口；其余按表面类型映射
                    std::string surfaceReturn;
                    if (method.returnsSelfRef)
                    {
                        surfaceReturn = shape.name;
                    }
                    else if (stringTwoStep)
                    {
                        surfaceReturn = "string";
                    }
                    else if (recordTwoStep)
                    {
                        surfaceReturn = surface.twoStepRecordShort;
                    }
                    else if (containerTwoStep)
                    {
                        surfaceReturn = csharpScalarTypeForWidth(surface.twoStepScalarElement) + "[]";
                    }
                    else if (!surfaceObjectName.empty())
                    {
                        surfaceReturn = surfaceObjectName;
                    }
                    else
                    {
                        surfaceReturn = toSurface(bare, returnPointer);
                    }
                    // 镜像记录按值返回：公开表面给同名记录类型（C ABI 是 `<T>C`，由 FromC 逐字段还原）。
                    // 公开记录都按类型共享产出（任务 2.3），所以不再按"有没有嵌套"过滤。
                    const MirrorType* surfaceMirrorReturn = nullptr;
                    if (!returnPointer)
                    {
                        const MirrorType* candidate = findMirrorByCpp(bare);
                        if (candidate != nullptr && surface.returnType == candidate->cName)
                        {
                            surfaceMirrorReturn = candidate;
                            surfaceReturn = candidate->csName;
                        }
                    }
                    // 方法级普通枚举返回：公开表面还原成枚举类型（ABI 仍是 int32_t）。
                    if (surfaceMirrorReturn == nullptr && method.returnKind == static_cast<int>(CXType_Enum))
                    {
                        EnumInfo const* info = findEnumBySpelling(method.returnCanonicalSpelling);
                        if (info != nullptr)
                        {
                            surfaceReturn = info->shortName;
                        }
                    }
                    // 认不出来的返回类型 ⇒ 这一条不进表面（打印原因，不静默跳过）
                    bool supported = !surfaceReturn.empty();

                    // 形参：出参收进元组返回值；字符串 / 字节 / 包装值 / 标志位 / 记录 / 智能指针
                    // 各自有确定的公开类型与 ABI 形状（任务 2.2 / 2.4 / 2.5 / 3.3）。
                    struct SurfaceParam
                    {
                        std::string type;
                        std::string name;
                        bool isOut = false;
                        bool isString = false;
                        bool isBool = false;
                        bool isMirror = false;
                        bool isEnum = false;
                        bool isFlags = false;
                        bool isBytes = false;
                        bool isHashObj = false;
                        bool isWrapperName = false;
                        bool isWrapperPath = false;
                        bool isSurfaceObject = false;   // shared_ptr<T> 入参：目标表面对象
                        bool isPointer = false;         // void* 入参：公开 IntPtr
                        std::string enumName;           // 枚举 / 标志位的公开类型名
                        std::string targetType;
                        std::string abiCastType;         // shared_ptr<T> 的目标完整限定名
                        MirrorType const* mirror = nullptr;
                        bool isValueClass = false;       // 资源引用值类（任务 3.1）
                        ValueTypeInfo const* valueInfo = nullptr;
                        bool isSerialized = false;       // 序列化块记录（任务 3.2）
                        SerializedTypeInfo const* serializedInfo = nullptr;
                    };
                    std::vector<SurfaceParam> surfaceParams;
                    std::vector<std::string> tupleParts;
                    for (std::size_t paramIndex = 0; paramIndex < method.params.size(); ++paramIndex)
                    {
                        auto const& parameter = method.params[paramIndex];
                        // 注入参数（任务 3.3）：托管表面里不出现。
                        if (method.injectedParams.count(parameter.second) > 0)
                        {
                            continue;
                        }
                        // 内存窗（任务 1.1 / design D2）：指针形参在托管表面收 `byte[]`，
                        // 长度由托管按数组长度算；被吸收的长度形参不在托管表面出现。
                        if (method.windowPointerByLength.count(paramIndex) > 0)
                        {
                            continue;
                        }
                        if (method.windowLengthByPointer.count(paramIndex) > 0)
                        {
                            SurfaceParam windowParam;
                            windowParam.type = "byte[]";
                            windowParam.name = parameter.second;
                            windowParam.isBytes = true;
                            surfaceParams.push_back(windowParam);
                            continue;
                        }
                        if (surface.isCallback)
                        {
                            // 回调 setter 的形参统一投影成 `Action<...>`（下面的专用分支），
                            // 这里不参与标量映射 —— 否则 `castl::function<...>` 会被判成"不可映射"整条跳过。
                            continue;
                        }
                        const std::string paramBare = bareType(parameter.first);
                        const std::string canonicalSpelling =
                            paramIndex < method.paramCanonicalSpellings.size()
                                ? method.paramCanonicalSpellings[paramIndex]
                                : parameter.first;
                        const bool isString = hasPrefix(paramBare, "castl::string_view") ||
                                              hasPrefix(paramBare, "std::string_view") ||
                                              paramBare == "castl::string" || paramBare == "std::string";
                        const bool isOutRef = parameter.first.find('&') != std::string::npos &&
                                              parameter.first.find("const") == std::string::npos;
                        // 方法级 EnumFlags<T>
                        {
                            std::string flagsInner;
                            if (parseFlagsWrapper(paramBare, &flagsInner) ||
                                parseFlagsWrapper(canonicalSpelling, &flagsInner))
                            {
                                EnumInfo const* info = findEnumByShort(flagsInner);
                                if (info == nullptr)
                                {
                                    supported = false;
                                    break;
                                }
                                SurfaceParam flagsParam;
                                flagsParam.type = info->shortName;
                                flagsParam.name = parameter.second;
                                flagsParam.isFlags = true;
                                flagsParam.enumName = info->shortName;
                                flagsParam.abiCastType = cSharpIntegerType(info->underlying);
                                surfaceParams.push_back(flagsParam);
                                continue;
                            }
                        }
                        // 方法级普通枚举
                        if (paramIndex < method.paramKinds.size() &&
                            method.paramKinds[paramIndex] == static_cast<int>(CXType_Enum))
                        {
                            EnumInfo const* info = findEnumBySpelling(canonicalSpelling);
                            if (info == nullptr)
                            {
                                supported = false;
                                break;
                            }
                            SurfaceParam enumParam;
                            enumParam.type = info->shortName;
                            enumParam.name = parameter.second;
                            enumParam.isEnum = true;
                            enumParam.enumName = info->shortName;
                            surfaceParams.push_back(enumParam);
                            continue;
                        }
                        // 字节入参
                        if (isByteVectorSpelling(parameter.first))
                        {
                            SurfaceParam bytesParam;
                            bytesParam.type = "byte[]";
                            bytesParam.name = parameter.second;
                            bytesParam.isBytes = true;
                            surfaceParams.push_back(bytesParam);
                            continue;
                        }
                        // 独立包装值
                        if (isNameHashSpelling(parameter.first) || isPathHashSpelling(parameter.first))
                        {
                            SurfaceParam wrapper;
                            wrapper.type = "string";
                            wrapper.name = parameter.second;
                            wrapper.isString = true;
                            wrapper.isWrapperName = isNameHashSpelling(parameter.first);
                            wrapper.isWrapperPath = isPathHashSpelling(parameter.first);
                            surfaceParams.push_back(wrapper);
                            continue;
                        }
                        // HashObj<T>
                        {
                            std::string hashInner;
                            if (parseHashObjWrapper(parameter.first, &hashInner))
                            {
                                const MirrorType* innerMirror = findMirrorByCpp(bareType(hashInner));
                                if (innerMirror == nullptr)
                                {
                                    supported = false;
                                    break;
                                }
                                SurfaceParam hashParam;
                                hashParam.type = innerMirror->csName;
                                hashParam.name = parameter.second;
                                hashParam.isHashObj = true;
                                hashParam.mirror = innerMirror;
                                surfaceParams.push_back(hashParam);
                                continue;
                            }
                        }
                        // shared_ptr<T> 入参：T 是已绑定接口时才形成公开目标表面对象
                        if (hasPrefix(paramBare, "castl::shared_ptr") || hasPrefix(paramBare, "std::shared_ptr"))
                        {
                            const std::size_t open = paramBare.find('<');
                            const std::size_t close = paramBare.rfind('>');
                            if (open == std::string::npos || close == std::string::npos || close <= open)
                            {
                                supported = false;
                                break;
                            }
                            const std::string inner = trimCopy(paramBare.substr(open + 1, close - open - 1));
                            const std::string innerShort = unqualifiedName(bareType(inner));
                            const std::string knownQualified =
                                knownInterfaceQualified(innerShort, shape.qualifiedName);
                            if (knownQualified.empty())
                            {
                                supported = false;
                                break;
                            }
                            SurfaceParam surfaceParam;
                            surfaceParam.type = innerShort;
                            surfaceParam.name = parameter.second;
                            surfaceParam.isSurfaceObject = true;
                            surfaceParam.targetType = knownQualified;
                            surfaceParams.push_back(surfaceParam);
                            continue;
                        }
                        // 资源引用值类（任务 3.1）：公开表面收同名托管值类型，ABI 侧是
                        // kind + 句柄 + 名称 ptr/len 四段。
                        {
                            auto valueType = g_valueTypes.find(paramBare);
                            if (valueType != g_valueTypes.end())
                            {
                                SurfaceParam valueParam;
                                valueParam.type = valueType->second.shortName;
                                valueParam.name = parameter.second;
                                valueParam.isValueClass = true;
                                valueParam.valueInfo = &valueType->second;
                                surfaceParams.push_back(valueParam);
                                continue;
                            }
                        }
                        // 序列化块记录（任务 3.2）：公开表面收托管镜像类型，ABI 侧是字节 + 长度。
                        {
                            auto serialized = g_serializedTypes.find(paramBare);
                            if (serialized != g_serializedTypes.end())
                            {
                                SurfaceParam serializedParam;
                                serializedParam.type = serialized->second.shortName;
                                serializedParam.name = parameter.second;
                                serializedParam.isSerialized = true;
                                serializedParam.serializedInfo = &serialized->second;
                                surfaceParams.push_back(serializedParam);
                                continue;
                            }
                        }
                        // 纯数据记录镜像的 `const&` 入参：公开表面收同名记录类型，ABI 侧是 `<T>C const*`。
                        const MirrorType* paramMirror = findMirrorByCpp(paramBare);
                        if (paramMirror != nullptr && parameter.first.find("const") != std::string::npos)
                        {
                            SurfaceParam mirrorParam;
                            mirrorParam.type = paramMirror->csName;
                            mirrorParam.name = parameter.second;
                            mirrorParam.isMirror = true;
                            mirrorParam.mirror = paramMirror;
                            surfaceParams.push_back(mirrorParam);
                            continue;
                        }
                        if (isString)
                        {
                            SurfaceParam stringParam;
                            stringParam.type = "string";
                            stringParam.name = parameter.second;
                            stringParam.isString = true;
                            surfaceParams.push_back(stringParam);
                            continue;
                        }
                        if (isOutRef)
                        {
                            SurfaceParam outParam;
                            outParam.type = toSurface(paramBare, false);
                            if (outParam.type.empty())
                            {
                                supported = false;
                                break;
                            }
                            outParam.name = parameter.second;
                            outParam.isOut = true;
                            surfaceParams.push_back(outParam);
                            std::string element = parameter.second;
                            if (hasPrefix(element, "out") && element.size() > 3)
                            {
                                element = element.substr(3);
                            }
                            tupleParts.push_back(outParam.type + " " + capitalize(element));
                            continue;
                        }
                        // `void*` 入参：公开 IntPtr，ABI 仍是不透明指针。
                        if (paramBare == "void" && parameter.first.find('*') != std::string::npos)
                        {
                            SurfaceParam pointerParam;
                            pointerParam.type = "IntPtr";
                            pointerParam.name = parameter.second;
                            pointerParam.isPointer = true;
                            surfaceParams.push_back(pointerParam);
                            continue;
                        }
                        SurfaceParam scalar;
                        scalar.type = toSurface(paramBare, false);
                        if (scalar.type.empty())
                        {
                            supported = false;
                            break;
                        }
                        scalar.name = parameter.second;
                        scalar.isBool = paramBare == "bool";
                        surfaceParams.push_back(scalar);
                    }
                    // 返回值：出参在场就换成元组（只有一个出参时不套元组）。
                    // 自返回（任务 2.2）：Calls 层是 void（它不能写 `this`），表面层单独返回 this。
                    std::string callReturn = surfaceReturn;
                    if (method.returnsSelfRef)
                    {
                        callReturn = "void";
                    }
                    else if (tupleParts.size() == 1)
                    {
                        callReturn = tupleParts[0];
                    }
                    else if (!tupleParts.empty())
                    {
                        callReturn = "(";
                        for (std::size_t i = 0; i < tupleParts.size(); ++i)
                        {
                            callReturn += (i == 0 ? "" : ", ") + tupleParts[i];
                        }
                        callReturn += ")";
                    }
                    if (!supported)
                    {
                        std::fprintf(stderr,
                                     "[托管表面] 跳过 %s::%s —— 返回/形参里有本轮不映射的形状（原始拼写：%s）\n",
                                     shape.name.c_str(), method.name.c_str(), method.returnSpelling.c_str());
                        continue;
                    }

                    if (surface.isCallback)
                    {
                        // 回调 setter（design D1/D2）：公开表面仍是 `void SetX(Action<...>)`；
                        // 安装入口返回的内部注册号、函数指针、用户令牌与保活项全留在 internal 层。
                        std::string actionType = "Action<";
                        for (std::size_t argIndex = 0; argIndex < surface.callback.shape.args.size(); ++argIndex)
                        {
                            actionType +=
                                (argIndex == 0 ? "" : ", ") + surface.callback.shape.args[argIndex].surfaceType;
                        }
                        actionType += ">";
                        const std::string throwLine = "            " + bindingsName +
                                                      ".ThrowIfFailed(lease.Self);\n";
                        const std::string bridgeMethod = "On" + method.name;
                        cs += "        public static void " + method.name + "(" + leaseName + " lease, " + shape.name +
                              "CallbackSlot slot, " + actionType + " callback, string methodName)\n        {\n";
                        cs += "            if (callback == null)\n            {\n";
                        cs += "                " + bindingsName + "." + method.name + "(lease.Self, null, null);\n";
                        cs += throwLine;
                        cs += "                if (slot.Registration != 0UL)\n                {\n";
                        cs += "                    " + bindingsName + ".UnregisterCallback(lease.Self, slot.Registration);\n";
                        cs += "                    slot.Registration = 0UL;\n                }\n";
                        cs += "                slot.Release();\n                return;\n            }\n";
                        cs += "            " + shape.name + "CallbackState state = new " + shape.name + "CallbackState();\n";
                        cs += "            state.Callback = callback;\n            state.Self = lease.Self;\n";
                        cs += "            state.ThreadId = Environment.CurrentManagedThreadId;\n";
                        cs += "            state.Method = methodName;\n";
                        cs += "            GCHandle keepAlive = GCHandle.Alloc(state);\n";
                        cs += "            ulong registration;\n            try\n            {\n";
                        cs += "                registration = " + bindingsName + "." + method.name + "(lease.Self, &" +
                              shape.name + "Callbacks." + bridgeMethod + ", (void*)GCHandle.ToIntPtr(keepAlive));\n";
                        cs += throwLine;
                        cs += "            }\n            catch\n            {\n";
                        cs += "                if (keepAlive.IsAllocated) { keepAlive.Free(); }\n";
                        cs += "                throw;\n            }\n";
                        cs += "            if (registration == 0UL)\n            {\n";
                        cs += "                if (keepAlive.IsAllocated) { keepAlive.Free(); }\n";
                        cs += "                throw new " + shape.name + "Exception(" + bindingsName +
                              ".ERROR_CALLBACK_EXCEPTION, \"回调安装没有拿到内部注册号\");\n            }\n";
                        cs += "            ulong superseded = slot.Registration;\n";
                        cs += "            slot.Retain(keepAlive);\n";
                        cs += "            slot.Registration = registration;\n";
                        cs += "            if (superseded != 0UL) { " + bindingsName +
                              ".UnregisterCallback(lease.Self, superseded); }\n";
                        cs += "        }\n\n";
                        surfaceClass += "        public void " + method.name + "(" + actionType + " callback)\n        {\n";
                        surfaceClass += "            " + callsName + "." + method.name + "(m_lease, m_callbackSlots[" +
                                         std::to_string(callbackIndex) + "], callback, \"" + method.name + "\");\n";
                        surfaceClass += "        }\n";
                        continue;
                    }

                    // ---- `<I>Calls.<M>` ----
                    std::string signature = method.name + "(" + leaseName + " lease";
                    std::string surfaceSignature = method.name + "(";
                    for (std::size_t i = 0; i < surfaceParams.size(); ++i)
                    {
                        if (surfaceParams[i].isOut)
                        {
                            continue;
                        }
                        const std::string tail = surfaceParams[i].type + " " + surfaceParams[i].name;
                        signature += ", " + tail;
                        surfaceSignature += (surfaceSignature.back() == '(' ? "" : ", ") + tail;
                    }
                    signature += ")";
                    surfaceSignature += ")";

                    cs += "        public static " + callReturn + " " + signature + "\n        {\n";
                    // 已释放的表面对象：调用 MUST 在托管侧被挡下（ObjectDisposedException），不得进入 thunk。
                    cs += "            if (lease.Disposed) { throw new ObjectDisposedException(\"" + shape.name +
                          "\"); }\n";
                    if (isBorrowable)
                    {
                        // 借用对象只在回调执行期间可用：任何方法进 native 之前先查作用域。
                        cs += "            if (lease.Scope != null && !lease.Scope.Active)\n            {\n";
                        cs += "                throw new " + shape.name +
                              "Exception(1001, \"回调借用对象已在回调返回后失效\");\n            }\n";
                    }
                    // 出参 / 字符串 / 字节 / 记录 / HashObj / 目标表面对象的调用前准备
                    for (SurfaceParam const& param : surfaceParams)
                    {
                        if (param.isOut)
                        {
                            cs += "            " + param.type + " " + param.name + " = default;\n";
                        }
                        else if (param.isString)
                        {
                            cs += "            byte[] " + param.name + "Bytes = System.Text.Encoding.UTF8.GetBytes(" +
                                  param.name + ");\n";
                        }
                        else if (param.isBytes)
                        {
                            cs += "            if (" + param.name + " == null)\n            {\n";
                            cs += "                throw new ArgumentNullException(nameof(" + param.name + "));\n";
                            cs += "            }\n";
                        }
                        else if (param.isValueClass)
                        {
                            // 资源引用值类：名称部分按 UTF-8 走 `byte[]`，调原生时用 fixed 固定。
                            cs += "            byte[] " + param.name + "NameBytes = System.Text.Encoding.UTF8.GetBytes(" +
                                  param.name + ".Name ?? string.Empty);\n";
                        }
                        else if (param.isSerialized)
                        {
                            // 序列化块记录：托管侧先按同源字段清单序列化成 byte[]。
                            cs += "            byte[] " + param.name + "Bytes = " +
                                  param.serializedInfo->shortName + "Codec.Serialize(" + param.name +
                                  ");\n";
                        }
                        else if (param.isMirror || param.isHashObj)
                        {
                            // 公开记录 → C 形状；ABI 要的是它的地址（`<T>C const*`）
                            cs += "            " + shape.name + "Bindings." + param.mirror->cName + " " + param.name +
                                  "C = ToC" + param.mirror->csName + "(" + param.name + ");\n";
                        }
                        else if (param.isSurfaceObject)
                        {
                            // 目标表面对象的句柄：null / 已释放都不得进入原生实现。
                            cs += "            if (" + param.name + " == null)\n            {\n";
                            cs += "                throw new ArgumentNullException(nameof(" + param.name + "));\n";
                            cs += "            }\n";
                            cs += "            if (" + param.name + ".Disposed)\n            {\n";
                            cs += "                throw new ObjectDisposedException(nameof(" + param.name + "));\n";
                            cs += "            }\n";
                        }
                    }
                    // ABI 实参（与投影后的 params 顺序一致）
                    std::string abiArgs = "lease.Self";
                    for (SurfaceParam const& param : surfaceParams)
                    {
                        if (param.isString)
                        {
                            abiArgs += ", " + param.name + "Ptr, (ulong)" + param.name + "Bytes.Length";
                            continue;
                        }
                        if (param.isOut)
                        {
                            abiArgs += ", &" + param.name;
                            continue;
                        }
                        if (param.isMirror || param.isHashObj)
                        {
                            abiArgs += ", &" + param.name + "C";
                            continue;
                        }
                        if (param.isBytes)
                        {
                            abiArgs += ", " + param.name + "Ptr, (ulong)" + param.name + ".Length";
                            continue;
                        }
                        if (param.isValueClass)
                        {
                            abiArgs += ", " + param.name + ".Kind, " + param.name + ".Handle, " + param.name +
                                       "NamePtr, (ulong)" + param.name + "NameBytes.Length";
                            continue;
                        }
                        if (param.isSerialized)
                        {
                            abiArgs += ", " + param.name + "BytesPtr, (ulong)" + param.name +
                                       "Bytes.Length";
                            continue;
                        }
                        if (param.isFlags)
                        {
                            abiArgs += ", (" + param.abiCastType + ")" + param.name;
                            continue;
                        }
                        if (param.isEnum)
                        {
                            abiArgs += ", (int)" + param.name;
                            continue;
                        }
                        if (param.isSurfaceObject)
                        {
                            abiArgs += ", (ulong)" + param.name + ".Token";
                            continue;
                        }
                        if (param.isPointer)
                        {
                            abiArgs += ", (void*)" + param.name;
                            continue;
                        }
                        abiArgs += ", " + (param.isBool ? "(" + param.name + " ? 1 : 0)" : param.name);
                    }

                    std::string callBody;
                    const std::string throwLine = "            " + bindingsName +
                                                  ".ThrowIfFailed(lease.Self);\n";
                    if (stringTwoStep)
                    {
                        callBody += "            ulong length = " + bindingsName + "." + abiName +
                                    "Len(lease.Self);\n";
                        callBody += throwLine;
                        callBody += "            if (length == 0UL) { return string.Empty; }\n";
                        callBody += "            byte[] buffer = new byte[(int)length + 1];\n";
                        callBody += "            fixed (byte* bufferPtr = buffer) { " + bindingsName + ".Copy" +
                                    abiName + "(lease.Self, bufferPtr, (ulong)buffer.Length); }\n";
                        callBody += throwLine;
                        callBody += "            return System.Text.Encoding.UTF8.GetString(buffer, 0, (int)length);\n";
                    }
                    else if (recordTwoStep)
                    {
                        // 记录两段式（D8）：Len 返回序列化字节数，0 表示原生返回了空指针 ⇒ 托管 null。
                        callBody += "            ulong length = " + bindingsName + "." + abiName + "Len(" +
                                    abiArgs + ");\n";
                        callBody += throwLine;
                        callBody += "            if (length == 0UL) { return null; }\n";
                        callBody += "            byte[] buffer = new byte[(int)length];\n";
                        callBody += "            fixed (byte* bufferPtr = buffer) { " + bindingsName + ".Copy" +
                                    abiName + "(" + abiArgs +
                                    ", bufferPtr, (ulong)buffer.Length); }\n";
                        callBody += throwLine;
                        callBody += "            return " + surface.twoStepRecordShort +
                                    "Codec.Deserialize(buffer);\n";
                    }
                    else if (containerTwoStep)
                    {
                        // 标量容器两段式（D13）：Count 查条数 → 分配数组 → 定长拷贝（按 byte* 传）。
                        // 空容器：count=0 直接返回空数组，不调 Copy（也不解引用）。
                        const std::string csElement =
                            csharpScalarTypeForWidth(surface.twoStepScalarElement);
                        const std::string elementBytes =
                            std::to_string(scalarWidthBytes(surface.twoStepScalarElement));
                        callBody += "            ulong count = " + bindingsName + "." + abiName +
                                    "Count(lease.Self);\n";
                        callBody += throwLine;
                        callBody += "            if (count == 0UL) { return new " + csElement +
                                    "[0]; }\n";
                        callBody += "            " + csElement + "[] values = new " + csElement +
                                    "[(int)count];\n";
                        callBody += "            fixed (" + csElement + "* valuesPtr = values) { " +
                                    bindingsName + ".Copy" + abiName +
                                    "(lease.Self, (byte*)valuesPtr, (ulong)values.Length * " +
                                    elementBytes + "UL); }\n";
                        callBody += throwLine;
                        callBody += "            return values;\n";
                    }
                    else if (!surfaceObjectName.empty())
                    {
                        // 强/弱/借用返回：C ABI 给的是句柄，这里用**目标接口**的解析/释放入口换成表面对象。
                        // 目标类型不符、已失效、借用无见证都会让解析返回 null 并把具体文本写进错误通道。
                        callBody += "            ulong handle = " + bindingsName + "." + abiName + "(" + abiArgs +
                                    ");\n";
                        callBody += throwLine;
                        callBody += "            void* self = " + surfaceObjectName +
                                    "Bindings.ResolveTargetHandle(lease.Self, handle);\n";
                        callBody += "            " + surfaceObjectName + "Bindings.ThrowIfFailed(lease.Self);\n";
                        callBody += "            if (self == null) { throw new " + shape.name +
                                    "Exception(1001, \"目标句柄已失效、类型不符或缺少存活见证\"); }\n";
                        callBody += "            return new " + surfaceObjectName + "(new " + surfaceObjectName +
                                    "Lease { Self = self, Handle = handle, "
                                    "ReleaseSelf = lease.Self, Release = " + surfaceObjectName +
                                    "Bindings.ReleaseTargetHandle });\n";
                    }
                    else if (!tupleParts.empty())
                    {
                        callBody += "            " + bindingsName + "." + abiName + "(" + abiArgs + ");\n";
                        callBody += throwLine;
                        callBody += "            return (";
                        bool firstOut = true;
                        for (SurfaceParam const& param : surfaceParams)
                        {
                            if (!param.isOut)
                            {
                                continue;
                            }
                            callBody += (firstOut ? "" : ", ") + param.name;
                            firstOut = false;
                        }
                        callBody += ");\n";
                    }
                    else if (method.returnsSelfRef)
                    {
                        // 自返回（任务 2.2）：Calls 层只负责调 native（void），表面层返回 this。
                        callBody += "            " + bindingsName + "." + abiName + "(" + abiArgs + ");\n";
                        callBody += throwLine;
                    }
                    else if (surface.returnType == "void")
                    {
                        callBody += "            " + bindingsName + "." + abiName + "(" + abiArgs + ");\n";
                        callBody += throwLine;
                    }
                    else if (surfaceMirrorReturn != nullptr)
                    {
                        // 镜像记录按值返回：C ABI 给 `<接口>Bindings.<T>C`，FromC 还原成公开记录
                        callBody += "            " + shape.name + "Bindings." + surfaceMirrorReturn->cName +
                                    " result = " + bindingsName + "." + abiName + "(" + abiArgs + ");\n";
                        callBody += throwLine;
                        callBody += "            return FromC" + surfaceMirrorReturn->csName + "(result);\n";
                    }
                    else if (method.returnKind == static_cast<int>(CXType_Enum))
                    {
                        // 普通枚举返回：ABI 是 int32_t，公开表面还原成枚举类型
                        EnumInfo const* info = findEnumBySpelling(method.returnCanonicalSpelling);
                        callBody += "            int result = " + bindingsName + "." + abiName + "(" + abiArgs +
                                    ");\n";
                        callBody += throwLine;
                        callBody += "            return (" + (info != nullptr ? info->shortName : std::string("int")) +
                                    ")result;\n";
                    }
                    else if (surface.returnType == "uint64_t")
                    {
                        // `void*` → 不透明令牌 → IntPtr
                        callBody += "            ulong value = " + bindingsName + "." + abiName + "(" + abiArgs +
                                    ");\n";
                        callBody += throwLine;
                        callBody += "            return (IntPtr)(void*)value;\n";
                    }
                    else
                    {
                        callBody += "            " + toCSharpType(surface.returnType) + " result = " + bindingsName + "." +
                                    abiName + "(" + abiArgs + ");\n";
                        callBody += throwLine;
                        if (bare == "bool")
                        {
                            callBody += "            return result != 0;\n";
                        }
                        else
                        {
                            callBody += "            return result;\n";
                        }
                    }
                    // 字符串 / 字节数组入参：`fixed` 包住整段原生调用（多个入参从后往前包）
                    for (auto it = surfaceParams.rbegin(); it != surfaceParams.rend(); ++it)
                    {
                        if (it->isString)
                        {
                            callBody = "            fixed (byte* " + it->name + "Ptr = " + it->name +
                                       "Bytes)\n            {\n" + callBody + "            }\n";
                        }
                        else if (it->isBytes)
                        {
                            callBody = "            fixed (byte* " + it->name + "Ptr = " + it->name +
                                       ")\n            {\n" + callBody + "            }\n";
                        }
                        else if (it->isValueClass)
                        {
                            callBody = "            fixed (byte* " + it->name + "NamePtr = " + it->name +
                                       "NameBytes)\n            {\n" + callBody + "            }\n";
                        }
                        else if (it->isSerialized)
                        {
                            callBody = "            fixed (byte* " + it->name + "BytesPtr = " + it->name +
                                       "Bytes)\n            {\n" + callBody + "            }\n";
                        }
                    }
                    cs += callBody;
                    cs += "        }\n\n";

                    // ---- 表面方法：一行转发（自返回方法转给 Calls 后返回 this）----
                    const std::string surfaceCallReturn = method.returnsSelfRef ? shape.name : callReturn;
                    surfaceClass += "        public " + surfaceCallReturn + " " + surfaceSignature +
                                    "\n        {\n";
                    surfaceClass += (callReturn == "void" ? "            " : "            return ") + callsName + "." +
                                    method.name + "(m_lease";
                    for (SurfaceParam const& param : surfaceParams)
                    {
                        if (!param.isOut)
                        {
                            surfaceClass += ", " + param.name;
                        }
                    }
                    surfaceClass += ");\n";
                    if (method.returnsSelfRef)
                    {
                        surfaceClass += "            return this;\n";
                    }
                    surfaceClass += "        }\n";
                }
                surfaceClass += "    }\n";
                cs += "    }\n"; // 关掉 <I>Calls
                if (hasCallbacks)
                {
                    // ---- 回调槽 / 状态 / 托管桥（本 change 任务 2.1 / 4.1–4.3）----
                    cs += "    /// <summary>回调槽：内部注册号 + 托管保活（GCHandle）。Retain/Release 维护保活计数。</summary>\n";
                    cs += "    internal sealed class " + shape.name + "CallbackSlot\n    {\n";
                    cs += "        public static int LiveKeepAliveCount;\n";
                    cs += "        public ulong Registration;\n        public GCHandle KeepAlive;\n";
                    cs += "        public void Retain(GCHandle handle)\n        {\n";
                    cs += "            if (KeepAlive.IsAllocated) { KeepAlive.Free(); --LiveKeepAliveCount; }\n";
                    cs += "            KeepAlive = handle;\n            ++LiveKeepAliveCount;\n        }\n";
                    cs += "        public void Release()\n        {\n";
                    cs += "            if (KeepAlive.IsAllocated) { KeepAlive.Free(); --LiveKeepAliveCount; }\n";
                    cs += "            Registration = 0UL;\n        }\n    }\n\n";
                    cs += "    /// <summary>回调状态：用户委托 + 注册它的实例 + 注册线程 + 方法名（错误文本用）</summary>\n";
                    cs += "    internal sealed unsafe class " + shape.name + "CallbackState\n    {\n";
                    cs += "        public Delegate Callback;\n        public void* Self;\n";
                    cs += "        public int ThreadId;\n        public string Method;\n    }\n\n";
                    cs += "    /// <summary>原生函数指针落到这里：非注册线程拒绝进入用户委托，异常就地捕获后写错误槽。</summary>\n";
                    cs += "    internal static unsafe class " + shape.name + "Callbacks\n    {\n";
                    for (CallbackMethod const& callback : callbackMethods)
                    {
                        const CallbackShape& callbackShape = callback.info.shape;
                        std::string bridgeParams = "void* user";
                        std::string delegateTypes;
                        std::string delegateArgs;
                        std::set<std::string> borrowScopes;
                        for (std::size_t argIndex = 0; argIndex < callbackShape.args.size(); ++argIndex)
                        {
                            const CallbackArg& argument = callbackShape.args[argIndex];
                            const std::string argName = "arg" + std::to_string(argIndex);
                            std::string bridgeType;
                            if (!argument.interfaceName.empty()) { bridgeType = "void*"; }
                            else if (argument.cAbiType == "int32_t" || argument.cAbiType == "int") { bridgeType = "int"; }
                            else if (argument.cAbiType == "uint32_t") { bridgeType = "uint"; }
                            else if (argument.cAbiType == "float") { bridgeType = "float"; }
                            else if (argument.cAbiType == "double") { bridgeType = "double"; }
                            else if (argument.cAbiType == "uint8_t") { bridgeType = "byte"; }
                            else { bridgeType = "void*"; }
                            bridgeParams += ", " + bridgeType + " " + argName;
                            delegateTypes += (argIndex == 0 ? "" : ", ") + argument.surfaceType;
                            std::string forwarded;
                            if (!argument.interfaceName.empty())
                            {
                                // 借用对象（design D3）：回调期间可用；作用域在回调返回后统一失效。
                                borrowScopes.insert(argument.interfaceName);
                                forwarded = "(" + argName + " == null ? null : new " + argument.interfaceName +
                                            "(new " + argument.interfaceName + "Lease { Self = " + argName +
                                            ", Scope = scope_" + argument.interfaceName + " }))";
                            }
                            else if (bareType(argument.cppType) == "bool")
                            {
                                forwarded = argName + " != 0";
                            }
                            else
                            {
                                forwarded = argName;
                            }
                            delegateArgs += (delegateArgs.empty() ? "" : ", ") + forwarded;
                        }
                        cs += "        [UnmanagedCallersOnly]\n";
                        cs += "        public static void On" + callback.name + "(" + bridgeParams + ")\n        {\n";
                        cs += "            " + shape.name + "CallbackState state;\n";
                        cs += "            try { state = (" + shape.name +
                              "CallbackState)GCHandle.FromIntPtr((IntPtr)user).Target; }\n";
                        cs += "            catch (Exception) { return; }\n";
                        cs += "            if (state == null) { return; }\n";
                        cs += "            if (Environment.CurrentManagedThreadId != state.ThreadId)\n            {\n";
                        cs += "                " + shape.name + "Bindings.ReportCallbackErrorText(state.Self, " +
                              shape.name + "Bindings.ERROR_CALLBACK_WRONG_THREAD, \"回调来自非注册线程：\" + state.Method);\n";
                        cs += "                return;\n            }\n";
                        for (std::string const& scope : borrowScopes)
                        {
                            cs += "            " + scope + "BorrowScope scope_" + scope + " = new " + scope +
                                  "BorrowScope();\n";
                        }
                        cs += "            try\n            {\n";
                        cs += "                ((Action<" + delegateTypes + ">)state.Callback)(" + delegateArgs + ");\n";
                        cs += "            }\n            catch (Exception ex)\n            {\n";
                        cs += "                " + shape.name + "Bindings.ReportCallbackErrorText(state.Self, " +
                              shape.name +
                              "Bindings.ERROR_CALLBACK_EXCEPTION, ex.GetType().Name + \": \" + ex.Message);\n";
                        cs += "            }\n";
                        if (!borrowScopes.empty())
                        {
                            // 回调返回即撤销借用：无论委托正常返回还是抛异常，作用域都要失效。
                            cs += "            finally\n            {\n";
                            for (std::string const& scope : borrowScopes)
                            {
                                cs += "                scope_" + scope + ".Active = false;\n";
                            }
                            cs += "            }\n";
                        }
                        cs += "        }\n";
                    }
                    cs += "    }\n\n";
                }
                cs += surfaceClass;
            }
            cs += "}\n";   // 关掉 namespace（各类型都是平级的，`<I>Bindings` 早在上面的块里关掉了）
            methodsTotal += emitted;
            // 一条进表方法都没有的接口（两段式字符串返回、或被 CA_BIND_SKIP 全排除）**也要落地**。
            // 以前这里是 `if (emitted == 0) continue;` —— 产物目录不清空，上一轮的旧文件就留在原地，
            // 判据读的是那几个字节，于是"这次一个字都没生成"与"生成得完全正确"在磁盘上长得一样。
            // 实测：`IBindRow04` 的两个两段式伴生因此从来没被真正生成过，判据却一直是绿的。
            const std::string cppPath = std::string(outDir) + "/" + linear + ".Bindings.cpp";
            const std::string csPath = std::string(outDir) + "/" + linear + ".Bindings.cs";
            std::ofstream cppOut(cppPath, std::ios::binary);
            cppOut << cpp;
            if (!cppOut)
            {
                std::fprintf(stderr, "CodeGen: cannot write '%s'\n", cppPath.c_str());
                return 2;
            }
            std::ofstream csOut(csPath, std::ios::binary);
            csOut << cs;
            if (!csOut)
            {
                std::fprintf(stderr, "CodeGen: cannot write '%s'\n", csPath.c_str());
                return 2;
            }
            writtenCsPaths.push_back(csPath);
            ++products;
        }

        // ---- 共享公开值类型（任务 2.3）----
        // 记录：所有镜像类型都出现在某个接口的公开参数/返回里，按限定名去重后各发一份。
        // 枚举：方法级参数/返回用到的枚举，以及记录字段里的枚举。
        {
            std::map<std::string, MirrorType> publicRecords;
            for (auto const& entry : mirrorsByInterface)
            {
                for (MirrorType const& mirror : entry.second)
                {
                    publicRecords.emplace(mirror.cppName, mirror);
                }
            }
            std::map<std::string, EnumInfo> publicEnums;
            auto noteEnum = [&publicEnums](std::string const& spelling, bool asFlags) {
                EnumInfo const* info = findEnumBySpelling(spelling);
                if (info == nullptr)
                {
                    return;
                }
                auto inserted = publicEnums.emplace(info->shortName, *info);
                if (!inserted.second && asFlags)
                {
                    inserted.first->second.usedAsFlags = true;
                }
            };
            for (InterfaceShape const& shape : shapes)
            {
                for (MethodSignature const& method : shape.methods)
                {
                    if (method.abiName.empty())
                    {
                        continue;   // 2.2 过渡：同名第二槽不绑
                    }
                    for (std::size_t index = 0; index < method.params.size(); ++index)
                    {
                        const std::string canonical =
                            index < method.paramCanonicalSpellings.size() ? method.paramCanonicalSpellings[index]
                                                                          : method.params[index].first;
                        if (index < method.paramKinds.size() &&
                            method.paramKinds[index] == static_cast<int>(CXType_Enum))
                        {
                            noteEnum(canonical, false);
                        }
                        std::string flagsInner;
                        if (parseFlagsWrapper(canonical, &flagsInner))
                        {
                            noteEnum(flagsInner, true);
                        }
                    }
                    if (method.returnKind == static_cast<int>(CXType_Enum))
                    {
                        noteEnum(method.returnCanonicalSpelling, false);
                    }
                }
            }
            for (auto const& entry : publicRecords)
            {
                for (MirrorField const& field : entry.second.fields)
                {
                    if (!field.isEnum)
                    {
                        continue;
                    }
                    auto inserted = publicEnums.emplace(field.enumShort, *findEnumByShort(field.enumShort));
                    if (!inserted.second && field.isFlags)
                    {
                        inserted.first->second.usedAsFlags = true;
                    }
                    if (field.isFlags)
                    {
                        inserted.first->second.usedAsFlags = true;
                    }
                }
            }
            // 序列化块记录字段里的枚举也要出公开枚举文件（托管镜像的类型引用要用）。
            for (auto const& entry : g_serializedTypes)
            {
                for (SerializedField const& field : entry.second.fields)
                {
                    auto noteSerializedEnum = [&publicEnums](std::string const& shortName) {
                        EnumInfo const* info = findEnumByShort(shortName);
                        if (info != nullptr)
                        {
                            publicEnums.emplace(info->shortName, *info);
                        }
                    };
                    if (field.kind == 1)
                    {
                        noteSerializedEnum(field.enumShort);
                    }
                    if (field.elementKind == 1)
                    {
                        noteSerializedEnum(field.elementEnumShort);
                    }
                }
            }
            // 容器元素记录与嵌套记录里的枚举同样要出公开枚举文件：元素记录的字段现在走统一类型
            // 映射（枚举 → 枚举类型名），不登记就会生成一个引用不存在类型的 .cs（实测 CS0246
            // `EShaderResourceAccess`）。
            {
                auto noteEnum = [&publicEnums](std::string const& shortName) {
                    EnumInfo const* info = findEnumByShort(shortName);
                    if (info != nullptr)
                    {
                        publicEnums.emplace(info->shortName, *info);
                    }
                };
                std::function<void(SerializedField const&)> visit;
                visit = [&](SerializedField const& field) {
                    if (field.kind == 1)
                    {
                        noteEnum(field.enumShort);
                    }
                    if (field.elementKind == 1)
                    {
                        noteEnum(field.elementEnumShort);
                    }
                    if (field.mapValueKind == 1)
                    {
                        noteEnum(field.mapValueEnumShort);
                    }
                    for (SerializedField const& sub : field.nestedFields)
                    {
                        visit(sub);
                    }
                    for (SerializedField const& sub : field.elementFields)
                    {
                        visit(sub);
                    }
                    for (SerializedField const& sub : field.mapValueFields)
                    {
                        visit(sub);
                    }
                };
                for (auto const& entry : g_serializedElementRecords)
                {
                    for (SerializedField const& field : entry.second.fields)
                    {
                        visit(field);
                    }
                }
            }
            for (auto const& entry : publicEnums)
            {
                const std::string linear = qualifiedLinearName(entry.second.cppName);
                const std::string path = std::string(outDir) + "/" + linear + ".Bindings.cs";
                std::ofstream out(path, std::ios::binary);
                out << emitPublicEnumFile(entry.second);
                if (!out)
                {
                    std::fprintf(stderr, "CodeGen: cannot write '%s'\n", path.c_str());
                    return 2;
                }
                writtenCsPaths.push_back(path);
                std::fprintf(stderr, "[共享类型] %s=%s\n", linear.c_str(),
                             entry.second.cppName.c_str());
            }
            for (auto const& entry : publicRecords)
            {
                const std::string linear = qualifiedLinearName(entry.second.cppName);
                const std::string path = std::string(outDir) + "/" + linear + ".Bindings.cs";
                std::ofstream out(path, std::ios::binary);
                out << emitPublicRecordFile(entry.second);
                if (!out)
                {
                    std::fprintf(stderr, "CodeGen: cannot write '%s'\n", path.c_str());
                    return 2;
                }
                writtenCsPaths.push_back(path);
                std::fprintf(stderr, "[共享类型] %s=%s\n", linear.c_str(),
                             entry.second.cppName.c_str());
            }
            // 资源引用值类（任务 3.1）：每个 CA_BIND_VALUE 各发一份共享托管值类型。
            for (auto const& entry : g_valueTypes)
            {
                const std::string linear = qualifiedLinearName(entry.second.qualifiedName);
                const std::string path = std::string(outDir) + "/" + linear + ".Bindings.cs";
                std::ofstream out(path, std::ios::binary);
                out << emitValueTypeCSharp(entry.second);
                if (!out)
                {
                    std::fprintf(stderr, "CodeGen: cannot write '%s'\n", path.c_str());
                    return 2;
                }
                writtenCsPaths.push_back(path);
                std::fprintf(stderr, "[共享类型] %s=%s\n", linear.c_str(),
                             entry.second.qualifiedName.c_str());
            }
            // 序列化块记录（任务 3.2）：C++ 编解码头 + 托管镜像/序列化器 + 元素记录镜像。
            for (auto const& entry : g_serializedTypes)
            {
                const SerializedTypeInfo& info = entry.second;
                const std::string linear = qualifiedLinearName(info.qualifiedName);
                const std::string codecPath = std::string(outDir) + "/" + linear + ".BindingCodec.h";
                std::ofstream codecOut(codecPath, std::ios::binary);
                codecOut << "#pragma once\n#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n"
                            "#include <vector>\n#include <HashKeys.h>\n\n";
                codecOut << emitSerializedCodecCpp(info);
                if (!codecOut)
                {
                    std::fprintf(stderr, "CodeGen: cannot write '%s'\n", codecPath.c_str());
                    return 2;
                }
                const std::string recordPath = std::string(outDir) + "/" + linear + ".Bindings.cs";
                std::ofstream recordOut(recordPath, std::ios::binary);
                recordOut << emitSerializedRecordCSharp(info);
                if (!recordOut)
                {
                    std::fprintf(stderr, "CodeGen: cannot write '%s'\n", recordPath.c_str());
                    return 2;
                }
                writtenCsPaths.push_back(recordPath);
                std::fprintf(stderr, "[共享类型] %s=%s\n", linear.c_str(),
                             info.qualifiedName.c_str());
            }
            for (auto const& entry : g_serializedElementRecords)
            {
                const std::string linear = qualifiedLinearName(entry.second.qualifiedName);
                const std::string path = std::string(outDir) + "/" + linear + ".Bindings.cs";
                std::ofstream out(path, std::ios::binary);
                out << emitSerializedElementCSharp(entry.second);
                if (!out)
                {
                    std::fprintf(stderr, "CodeGen: cannot write '%s'\n", path.c_str());
                    return 2;
                }
                writtenCsPaths.push_back(path);
                std::fprintf(stderr, "[共享类型] %s=%s\n", linear.c_str(),
                             entry.second.qualifiedName.c_str());
            }
        }
        // 注入片段（任务 3.3）：把"哪个接口的哪个方法用了哪个固定名钩子"写成 `# inject` 行，
        // 供装配侧核对。它不是登记表分片（不参与编号），单独一个 `<接口>.inject.part`。
        for (InterfaceShape const& shape : shapes)
        {
            const std::string linear = qualifiedLinearName(shape.qualifiedName);
            std::string injectText;
            for (MethodSignature const& method : shape.methods)
            {
                for (std::string const& parameterName : method.injectedParams)
                {
                    injectText += "# inject\t" +
                                  injectHookName(shape.qualifiedName, method.name, parameterName) + "\n";
                }
            }
            if (injectText.empty())
            {
                continue;
            }
            const std::string path = std::string(outDir) + "/" + linear + ".inject.part";
            std::ofstream injectOut(path, std::ios::binary);
            injectOut << injectText;
            if (!injectOut)
            {
                std::fprintf(stderr, "CodeGen: cannot write '%s'\n", path.c_str());
                return 2;
            }
            std::fprintf(stderr, "注入片段：%s（%zu 行）\n", path.c_str(),
                         std::count(injectText.begin(), injectText.end(), '\n'));
        }

        // ---- 跨命名空间 using 补齐（本批 1.1）----
        // 生成文件按镜像命名空间分层后，接口/记录引用别的命名空间里的类型需要 `using`。
        // 口径：文件里**按整词出现**的已知类型短名，若它的命名空间与本文件不同，就补一条 using。
        {
            std::map<std::string, std::string> namespaceByShort;
            auto noteType = [&namespaceByShort](std::string const& qualified) {
                const std::string prefix = namespacePrefixOf(qualified);
                if (prefix.empty())
                {
                    return;
                }
                namespaceByShort[qualified.substr(prefix.size())] = cSharpNamespaceOf(qualified);
            };
            for (auto const& entry : g_knownInterfaces) noteType(entry.second);
            for (auto const& entry : g_valueTypes) noteType(entry.second.qualifiedName);
            for (auto const& entry : g_serializedTypes) noteType(entry.second.qualifiedName);
            for (auto const& entry : g_serializedElementRecords) noteType(entry.second.qualifiedName);
            for (auto const& entry : g_enumsByShort) noteType(entry.second.cppName);
            for (auto const& entry : mirrorsByInterface)
            {
                for (auto const& mirror : entry.second)
                {
                    noteType(mirror.cppName);
                }
            }
            auto containsWord = [](std::string const& hay, std::string const& needle) {
                for (std::size_t pos = hay.find(needle); pos != std::string::npos;
                     pos = hay.find(needle, pos + needle.size()))
                {
                    const bool left = pos == 0 ||
                                      !(std::isalnum(static_cast<unsigned char>(hay[pos - 1])) ||
                                        hay[pos - 1] == '_');
                    const std::size_t end = pos + needle.size();
                    const bool right = end >= hay.size() ||
                                       !(std::isalnum(static_cast<unsigned char>(hay[end])) ||
                                         hay[end] == '_');
                    if (left && right)
                    {
                        return true;
                    }
                }
                return false;
            };
            for (std::string const& path : writtenCsPaths)
            {
                std::ifstream in(path, std::ios::binary);
                if (!in)
                {
                    continue;
                }
                std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                const std::size_t nsPos = text.find("namespace ");
                if (nsPos == std::string::npos)
                {
                    continue;
                }
                const std::size_t nsEnd = text.find('\n', nsPos);
                std::string current = text.substr(
                    nsPos + 10, nsEnd == std::string::npos ? std::string::npos : nsEnd - nsPos - 10);
                while (!current.empty() && (current.back() == '\r' || current.back() == ' '))
                {
                    current.pop_back();
                }
                if (!current.empty() && current.back() == '{')
                {
                    current.pop_back();
                    while (!current.empty() && current.back() == ' ')
                    {
                        current.pop_back();
                    }
                }
                std::string usings;
                std::set<std::string> added;
                for (auto const& entry : namespaceByShort)
                {
                    if (entry.second == current || entry.second.empty())
                    {
                        continue;
                    }
                    if (!containsWord(text, entry.first))
                    {
                        continue;
                    }
                    if (!added.insert(entry.second).second)
                    {
                        continue;
                    }
                    usings += "using " + entry.second + ";\n";
                }
                if (usings.empty())
                {
                    continue;
                }
                text.insert(nsPos, usings + "\n");
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out << text;
            }
        }

        std::fprintf(stderr, "绑定：接口 %zu 个 / 方法 %d 条 / 产物 %d 个 / 拒绝 %d 条\n",
                     shapes.size() + reverseShapes.size(), methodsTotal + reverseMethodsTotal, products, rejected);
        if (rejected > 0)
        {
            return 1;
        }
        // 登记表输入的分片（本 change 任务 1.2 / 1.3）：到这里所有接口产物都已成功写出，
        // 再把"编号 → 接口"的分片落到登记产物目录，供 `--emit-registry` 汇总。
        // 拒绝路径（上面的 registryProblems / rejected / 解析失败）在写分片之前就返回了 ——
        // 因此拒绝的那一轮不会留下任何登记分片，更不会改写已有登记产物。
        if (!registryPartLabel.empty())
        {
            std::string part;
            part += "# render-interface-merge 登记输入\n";
            part += "# header\t" + std::string(filePath != nullptr ? filePath : "") + "\n";
            int missingShapes = 0;
            for (BoundMarker const& marker : markers)
            {
                // 反向接口的形状在 `reverseShapes` 里（正向 `shapes` 故意不收它们）：登记分片
                // 两处都找，否则反向登记输入会被判"有标记但找不到类定义"、分片写不出来。
                InterfaceShape const* found = nullptr;
                for (InterfaceShape const& shape : shapes)
                {
                    if (shape.name == marker.interfaceName)
                    {
                        found = &shape;
                        break;
                    }
                }
                if (found == nullptr)
                {
                    for (InterfaceShape const& shape : reverseShapes)
                    {
                        if (shape.name == marker.interfaceName)
                        {
                            found = &shape;
                            break;
                        }
                    }
                }
                if (found == nullptr)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 %s —— 有 CA_BIND 标注，但找不到它的类定义\n",
                                 marker.interfaceName.c_str());
                    ++missingShapes;
                    continue;
                }
                auto number = registryNumbers.find(marker.interfaceName);
                if (number == registryNumbers.end())
                {
                    // 上面的锁核对已经把"没号"逐条报过；这里兜住"绕过核对"的路径。
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 %s —— 没有从锁文件取到编号（段 %s）\n",
                                 marker.interfaceName.c_str(), idScope.c_str());
                    ++missingShapes;
                    continue;
                }
                part += std::to_string(number->second) + "\t" + found->name + "\t" +
                        found->qualifiedName + "\t" + std::string(filePath != nullptr ? filePath : "") + "\n";
            }
            if (missingShapes > 0)
            {
                return 1;
            }
            // 注入钩子进分片（任务 3.3）：装配侧按这行核对钩子声明；有注入没钩子在生成期已被拒绝，
            // 这里保证"谁提供了哪个指针"在登记输入里可见。
            for (BoundMarker const& marker : markers)
            {
                auto owner = std::find_if(shapes.begin(), shapes.end(),
                                          [&marker](InterfaceShape const& shape) {
                                              return shape.name == marker.interfaceName;
                                          });
                const std::string ownerQualified =
                    owner == shapes.end() ? marker.interfaceName : owner->qualifiedName;
                for (auto const& injection : marker.injectParams)
                {
                    for (std::string const& parameterName : injection.second)
                    {
                        part += "# inject\t" +
                                injectHookName(ownerQualified, injection.first, parameterName) + "\n";
                    }
                }
            }
            const std::string partPath = registryPartDir + "/" + registryPartLabel + ".registry.part";
            std::ofstream partOut(partPath, std::ios::binary);
            partOut << part;
            if (!partOut)
            {
                std::fprintf(stderr, "CodeGen: cannot write '%s'\n", partPath.c_str());
                return 2;
            }
            std::fprintf(stderr, "登记分片：%s（%zu 个接口）\n", partPath.c_str(), markers.size());
        }
        // 没有产物不等于失败：整个接口的方法都被带理由排除掉时，0 个产物是正确的
        return 0;
    }

    // `--list-bind-interfaces`（csharp-render-binding-foundations 任务 3.1）：只解析主头里实际
    // `CA_BIND` 的类，按 `<短名>=<完整限定名>` 逐行输出。build.py 汇总多份生成输入的清单后，
    // 再以重复的 `--known-interface` 传给正式生成；清单本身不产出任何文件。
    int listBindInterfaces(const char* filePath, const std::vector<const char*>& clangArgs)
    {
        const std::string sourceText = readWholeFile(filePath);
        const std::vector<BoundMarker> markers = scanBoundMarkers(sourceText);
        if (markers.empty())
        {
            std::fprintf(stderr, "CodeGen: --list-bind-interfaces found no CA_BIND marker in '%s'.\n",
                         filePath);
            return 1;
        }
        // 带号标注在清单阶段就拒绝（任务 1.4）：不然后面的 --emit-binding 才是第一次报错，
        // 而 build.py 的"清单先跑"这一步会在产品目录留下旧分片，红态不够单因。
        for (BoundMarker const& marker : markers)
        {
            if (marker.hasNumber)
            {
                std::fprintf(stderr,
                             "CodeGen: 拒绝 %s —— 编号由锁文件治理、标注无参数（标注里写的是 \"%s\"）\n",
                             marker.interfaceName.c_str(), marker.numberText.c_str());
                return 1;
            }
        }
        std::vector<const char*> args;
        args.push_back("-std=c++20");
        args.push_back("-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH");
        for (const char* argument : clangArgs)
        {
            args.push_back(argument);
        }
        CXIndex index = clang_createIndex(1, 0);
        CXTranslationUnit unit = nullptr;
        const CXErrorCode error = clang_parseTranslationUnit2(
            index, filePath, args.data(), static_cast<int>(args.size()), nullptr, 0,
            CXTranslationUnit_DetailedPreprocessingRecord, &unit);
        if (error != CXError_Success || unit == nullptr)
        {
            std::fprintf(stderr, "CodeGen: libclang parse failed (error=%d) for '%s'\n",
                         static_cast<int>(error), filePath);
            clang_disposeIndex(index);
            return 3;
        }
        const unsigned diagnosticCount = clang_getNumDiagnostics(unit);
        unsigned errorCount = 0;
        for (unsigned i = 0; i < diagnosticCount; ++i)
        {
            CXDiagnostic diagnostic = clang_getDiagnostic(unit, i);
            if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error)
            {
                CXString text = clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions());
                std::fprintf(stderr, "  libclang: %s\n", clang_getCString(text));
                clang_disposeString(text);
                ++errorCount;
            }
            clang_disposeDiagnostic(diagnostic);
        }
        if (errorCount > 0)
        {
            clang_disposeTranslationUnit(unit);
            clang_disposeIndex(index);
            std::fprintf(stderr, "CodeGen: 清单解析有 %u 条错误诊断，拒绝输出清单。\n", errorCount);
            return 3;
        }
        std::vector<std::string> wanted;
        for (BoundMarker const& marker : markers)
        {
            wanted.push_back(marker.interfaceName);
        }
        std::vector<InterfaceShape> shapes;
        ClassCollector collector;
        collector.wanted = &wanted;
        collector.sink = &shapes;
        clang_visitChildren(clang_getTranslationUnitCursor(unit), collectClasses, &collector);
        clang_disposeTranslationUnit(unit);
        clang_disposeIndex(index);

        // 本批 1.1：清单按完整限定名逐条输出，同一个短名可以出现多行（跨命名空间同名），
        // 不再按短名拒绝、也不再把同短名的两条折叠成一条。只去掉完全相同的重复条目。
        std::vector<std::pair<std::string, std::string>> listed;
        for (BoundMarker const& marker : markers)
        {
            auto found = std::find_if(shapes.begin(), shapes.end(), [&marker](InterfaceShape const& shape) {
                return shape.name == marker.interfaceName;
            });
            if (found == shapes.end())
            {
                std::fprintf(stderr, "CodeGen: 清单里 %s 有 CA_BIND 标记，但找不到它的类定义\n",
                             marker.interfaceName.c_str());
                return 1;
            }
            bool alreadyListed = false;
            for (auto const& entry : listed)
            {
                if (entry.first == found->name && entry.second == found->qualifiedName)
                {
                    alreadyListed = true;
                    break;
                }
            }
            if (!alreadyListed)
            {
                listed.emplace_back(found->name, found->qualifiedName);
            }
        }
        for (auto const& entry : listed)
        {
            std::printf("%s=%s\n", entry.first.c_str(), entry.second.c_str());
        }
        return 0;
    }

    // ---- 登记表发射（render-interface-merge 任务 1.3）----
    // 输入是每个登记表输入头在 `--emit-binding --registry-part` 时写下的分片；
    // 输出是跨接口的三份产物：
    //   BindingRegistry.Bindings.h  编号清单、EngineApi struct + static_assert、宿主固定名钩子声明
    //   BindingRegistry.Bindings.cpp `get_interface_api` 路由、布局查询路由、EngineApi 装配
    //   EngineApi.Bindings.cs       与 C++ struct 逐字段对照的托管 struct + 生成常量
    struct RegistryEntry
    {
        unsigned number = 0;
        std::string shortName;
        std::string qualifiedName;
        std::string header;
    };

    struct EngineApiFieldSpec
    {
        char const* cppType;
        char const* cppName;
        char const* csType;
        char const* csName;
        char const* interfaceShort;   // 非空 = 该字段填对应接口的编号
        char const* hostSymbol;       // 非空 = 装配成宿主固定名函数
    };

    // 9 个字段的顺序与宽度就是 ABI（csharp-native-direct-binding 6.2：v10 重建 —— 桥五类的五个编号字段
    // 随退役删除，布局从 14 字段 / 80 字节缩到 9 字段 / 56 字节）。两侧的 struct 与装配代码都从这一张表发出。
    EngineApiFieldSpec const kEngineApiFields[] = {
        { "uint32_t", "abi_version", "uint", "AbiVersion", nullptr, nullptr },
        { "uint32_t", "size", "uint", "Size", nullptr, nullptr },
        { "void (*)(int level, uint8_t const* utf8, uint64_t len)",
          "log", "delegate* unmanaged<int, byte*, ulong, void>", "Log", nullptr, "CSharpHostManagedLog" },
        { "int32_t (*)(int32_t value)", "echo", "delegate* unmanaged<int, int>", "Echo", nullptr,
          "CSharpHostManagedEcho" },
        { "uint32_t (*)(uint32_t interfaceId, char const* instanceName, uint32_t version, "
          "uint32_t size, void* out, void** instanceOut)",
          "get_interface_api", "delegate* unmanaged<uint, byte*, uint, uint, void*, void**, uint>",
          "GetInterfaceApi", nullptr, nullptr },
        { "uint32_t", "window_interface_id", "uint", "WindowInterfaceId", "IWindow", nullptr },
        { "uint32_t", "window_system_interface_id", "uint", "WindowSystemInterfaceId", "IWindowSystem", nullptr },
        { "uint32_t (*)(uint32_t interfaceId, uint32_t typeIndex, char* name, "
          "uint32_t nameCap, uint64_t* sizeBytes, uint32_t* fieldCount)",
          "get_binding_layout_type",
          "delegate* unmanaged<uint, uint, byte*, uint, ulong*, uint*, uint>", "GetBindingLayoutType",
          nullptr, "CSharpHostBindingLayoutType" },
        { "uint32_t (*)(uint32_t interfaceId, uint32_t typeIndex, uint32_t fieldIndex, "
          "char* name, uint32_t nameCap, char* typeName, uint32_t typeCap, uint64_t* offsetBytes)",
          "get_binding_layout_field",
          "delegate* unmanaged<uint, uint, uint, byte*, uint, byte*, uint, ulong*, uint>",
          "GetBindingLayoutField", nullptr, "CSharpHostBindingLayoutField" },
    };

    // 把字段声明拼成合法 C++：函数指针类型的名字必须写在 `(*` 之后
    // （`void (*)(int)` + `log` ⇒ `void (*log)(int)`），不能像普通字段那样在后面再拼一个名字。
    std::string cppFieldDeclaration(EngineApiFieldSpec const& field)
    {
        const std::string type = field.cppType;
        const std::size_t star = type.find("(*)");
        if (star == std::string::npos)
        {
            return type + " " + field.cppName + ";";
        }
        return type.substr(0, star + 2) + field.cppName + type.substr(star + 2) + ";";
    }

    // C# 常量名里的命名空间段：按 D3② 用 PascalCase 段、以 `__` 分隔（GraphicsBackend__GPUGraphInterfaceId）。
    std::string cSharpNamespaceLinear(std::string const& qualifiedName)
    {
        const std::string prefix = namespacePrefixOf(qualifiedName);
        std::string out;
        std::size_t begin = 0;
        while (begin < prefix.size())
        {
            const std::size_t split = prefix.find("::", begin);
            const std::size_t end = split == std::string::npos ? prefix.size() : split;
            if (!out.empty())
            {
                out += "__";
            }
            out += cSharpNamespaceSegment(prefix.substr(begin, end - begin));
            if (split == std::string::npos)
            {
                break;
            }
            begin = split + 2;
        }
        return out;
    }

    // 接口完整限定名 + 短名 → 生成常量名 / C# 常量名：命名空间线形 + `__` + 去掉开头 I 的短名 + InterfaceId。
    std::string interfaceConstantSuffix(std::string const& qualifiedName, std::string const& shortName)
    {
        std::string bare = shortName;
        if (bare.size() > 1 && bare[0] == 'I' && std::isupper(static_cast<unsigned char>(bare[1])))
        {
            bare = bare.substr(1);
        }
        const std::string ns = cSharpNamespaceLinear(qualifiedName);
        return (ns.empty() ? std::string() : ns + "__") + bare + "InterfaceId";
    }

    bool readRegistryPart(std::string const& path, std::vector<RegistryEntry>* entries, std::string* error)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            *error = "读不到登记分片：" + path;
            return false;
        }
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::vector<std::string> fields;
            std::size_t begin = 0;
            for (;;)
            {
                const std::size_t tab = line.find('\t', begin);
                fields.push_back(line.substr(begin, tab == std::string::npos ? std::string::npos : tab - begin));
                if (tab == std::string::npos)
                {
                    break;
                }
                begin = tab + 1;
            }
            if (fields.size() != 4)
            {
                *error = "登记分片格式不对（" + path + "）：" + line;
                return false;
            }
            RegistryEntry entry;
            entry.number = static_cast<unsigned>(std::strtoul(fields[0].c_str(), nullptr, 10));
            entry.shortName = fields[1];
            entry.qualifiedName = fields[2];
            entry.header = fields[3];
            if (entry.number == 0 || entry.shortName.empty() || entry.qualifiedName.empty())
            {
                *error = "登记分片里有非法条目（" + path + "）：" + line;
                return false;
            }
            entries->push_back(std::move(entry));
        }
        return true;
    }

    int emitRegistry(std::string const& outDir, std::vector<std::string> const& partFiles,
                     std::string const& idLockPath, std::string const& idScope)
    {
        std::vector<RegistryEntry> entries;
        for (std::string const& part : partFiles)
        {
            std::string error;
            if (!readRegistryPart(part, &entries, &error))
            {
                std::fprintf(stderr, "CodeGen: %s\n", error.c_str());
                return 1;
            }
        }
        if (entries.empty())
        {
            std::fprintf(stderr, "CodeGen: --emit-registry 没有读到任何登记条目（分片 %zu 个）\n",
                         partFiles.size());
            return 1;
        }
        // 分片按**翻译单元**写：被别的头 include 的接口会在多个分片里各出现一次（实测
        // `WindowHandle.h` 被 `ShaderResourceHandle.h` 带进 CRenderBackend/GPUGraph/ShaderStruct
        // 的分片，聚合时报"编号 12 重复"）。同号同名的重复条目只要**四列完全一致**就是同一次
        // 登记，去重即可；任何一处不同才当冲突（下面的判据不变）。
        {
            std::vector<RegistryEntry> deduped;
            std::set<std::string> seen;
            for (RegistryEntry const& entry : entries)
            {
                const std::string key = std::to_string(entry.number) + "\t" + entry.shortName + "\t" +
                                        entry.qualifiedName + "\t" + entry.header;
                if (!seen.insert(key).second)
                {
                    continue;
                }
                deduped.push_back(entry);
            }
            if (deduped.size() != entries.size())
            {
                std::fprintf(stderr, "[登记] 去重：%zu 条重复分片条目（同号同名同头，来自被包含头）\n",
                             entries.size() - deduped.size());
            }
            entries = std::move(deduped);
        }
        std::sort(entries.begin(), entries.end(), [](RegistryEntry const& a, RegistryEntry const& b) {
            if (a.number != b.number)
            {
                return a.number < b.number;
            }
            return a.shortName < b.shortName;
        });
        // 编号与完整限定名都必须全局唯一：两个接口同号 / 同一完整限定名都必须在写产物之前拒绝并点名双方。
        // 本批 1.1：同短名不同命名空间是合法的，不再按短名拒绝。
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            for (std::size_t j = 0; j < i; ++j)
            {
                if (entries[i].number == entries[j].number)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 —— 编号 %u 重复：%s（%s）与 %s（%s）\n",
                                 entries[i].number, entries[j].qualifiedName.c_str(),
                                 entries[j].header.c_str(), entries[i].qualifiedName.c_str(),
                                 entries[i].header.c_str());
                    return 1;
                }
                if (entries[i].qualifiedName == entries[j].qualifiedName)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 —— 接口完整限定名 %s 重复：%s 与 %s\n",
                                 entries[i].qualifiedName.c_str(), entries[j].header.c_str(),
                                 entries[i].header.c_str());
                    return 1;
                }
            }
        }
        // ---- 锁文件核对（本 change 任务 1.2）：登记输入的接口名集合与锁内该段名集合完全相等 ----
        // 分片是"登记输入实际写出来的名字"，锁是"编号事实"。多一行、少一行、改号都必须点名。
        if (idLockPath.empty() || idScope.empty())
        {
            std::fprintf(stderr,
                         "CodeGen: --emit-registry 需要 --id-lock <锁文件> 与 --id-scope <host|fixture>\n");
            return 1;
        }
        {
            InterfaceLock lock;
            std::string lockError;
            if (!loadInterfaceLock(idLockPath, &lock, &lockError))
            {
                std::fprintf(stderr, "CodeGen: 拒绝登记 —— %s\n", lockError.c_str());
                return 1;
            }
            LockScope* scope = findLockScope(lock, idScope);
            if (scope == nullptr)
            {
                std::fprintf(stderr, "CodeGen: 拒绝登记 —— 锁文件里没有段 `%s`\n", idScope.c_str());
                return 1;
            }
            int lockProblems = 0;
            for (RegistryEntry const& entry : entries)
            {
                LockEntry const* locked = findLockEntry(*scope, entry.shortName, entry.qualifiedName);
                if (locked == nullptr)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 —— 分片里有 %s（编号 %u），锁文件（段 %s）里没有它\n",
                                 entry.shortName.c_str(), entry.number, idScope.c_str());
                    ++lockProblems;
                    continue;
                }
                if (locked->number != entry.number)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 —— %s 在分片里的编号是 %u，锁文件里是 %u\n",
                                 entry.shortName.c_str(), entry.number, locked->number);
                    ++lockProblems;
                }
                if (normalizeLockSlashes(locked->qualifiedName) !=
                    normalizeLockSlashes(entry.qualifiedName))
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 —— %s 的完整限定名对不上：分片 %s / 锁 %s\n",
                                 entry.shortName.c_str(), entry.qualifiedName.c_str(),
                                 locked->qualifiedName.c_str());
                    ++lockProblems;
                }
            }
            for (LockEntry const& locked : scope->entries)
            {
                bool present = false;
                for (RegistryEntry const& entry : entries)
                {
                    if (entry.shortName == locked.shortName)
                    {
                        present = true;
                        break;
                    }
                }
                if (!present)
                {
                    std::fprintf(stderr,
                                 "CodeGen: 拒绝登记 —— 锁文件（段 %s）里有 %s（编号 %u），但本次登记输入"
                                 "没有写出它（缺标注 / 输入漏了头）\n",
                                 idScope.c_str(), locked.shortName.c_str(), locked.number);
                    ++lockProblems;
                }
            }
            if (lockProblems > 0)
            {
                return 1;
            }
        }

        // `EngineApi` 的编号字段按接口短名找编号；找不到的字段装配成 0（该编号还没登记）。
        std::map<std::string, unsigned> numberByShort;
        std::map<std::string, std::string> qualifiedByShort;   // 装配常量名要用完整限定名线形
        for (RegistryEntry const& entry : entries)
        {
            numberByShort[entry.shortName] = entry.number;
            qualifiedByShort[entry.shortName] = entry.qualifiedName;
        }

        std::string header;
        header += "// 由 CodeGen --emit-registry 生成，勿手改。\n";
        header += "// 编号唯一来源：各接口头上的 CA_BIND(编号) 标注。\n";
        header += "// 登记输入（按编号排序）：\n";
        for (RegistryEntry const& entry : entries)
        {
            header += "//   " + std::to_string(entry.number) + " = " + entry.qualifiedName +
                      "（" + entry.header + "）\n";
        }
        header += "#pragma once\n#include <stdint.h>\n#include <stddef.h>\n\n";
        header += "// ---- 接口编号清单 ----\n";
        for (RegistryEntry const& entry : entries)
        {
            header += "inline constexpr uint32_t kBindingInterfaceId" +
                      qualifiedLinearName(entry.qualifiedName) + " = " +
                      std::to_string(entry.number) + "u;  // " + entry.qualifiedName + "\n";
        }
        header += "\n// 生成接口表的 ABI 版本（生成器固定 2u，本 change 不提升；窗口两表与渲染五表同版）\n";
        header += "inline constexpr uint32_t kBindingInterfaceTableVersion = 2u;\n";
        header += "// 跨边界函数表版本（csharp-bindings-namespace-alignment 4.1：v10 → v11；身份链改完整限定名，布局不变 9 字段 / 56 字节）\n";
        header += "inline constexpr uint32_t kEngineApiVersion = 12u;\n";
        header += "inline constexpr uint32_t kEngineApiSize = 56u;\n\n";
        header += "// 交给托管侧的函数表。字段顺序与宽度就是 ABI —— 只许在末尾追加。\n";
        header += "struct EngineApi\n{\n";
        for (EngineApiFieldSpec const& field : kEngineApiFields)
        {
            header += "    " + cppFieldDeclaration(field) + "\n";
        }
        header += "};\n\n";
        header += "static_assert(sizeof(EngineApi) == 56u,\n";
        header += "              \"生成的 EngineApi 必须是 56 字节（9 字段：4 + 4 + 8×5 + 4 + 4）\");\n";
        header += "static_assert(kEngineApiSize == 56u, \"kEngineApiSize 必须与 sizeof(EngineApi) 一致\");\n\n";
        header += "// 宿主固定名字的实现钩子：实现留手写（log / echo / 实例查找 / 错误出口 / 默认实例工厂 /\n";
        header += "// 角色登记），登记表只按固定名字调用它们 —— 逐接口分支不再存在。\n";
        header += "extern \"C\" void CSharpHostManagedLog(int level, uint8_t const* utf8, uint64_t len);\n";
        header += "extern \"C\" int32_t CSharpHostManagedEcho(int32_t value);\n";
        header += "extern \"C\" void CSharpHostBindingReportError(char const* text);\n";
        header += "extern \"C\" void* CSharpHostBindingLookupInstance(uint32_t interfaceId, char const* instanceName);\n";
        header += "extern \"C\" void CSharpHostBindingCreateDefaultInstances(void);\n";
        header += "// 返回 1 = 该编号要求默认实例；0 = 目标类型表（允许空实例）；-1 = 宿主没有登记该编号\n";
        header += "extern \"C\" int32_t CSharpHostBindingRequiresInstance(uint32_t interfaceId);\n";
        header += "// 布局查询由宿主固定名实现包一层：路由本身是生成的，宿主那层只留自检钩子\n";
        header += "// （layout-mismatch 构造要在引擎侧改偏移）。\n";
        header += "extern \"C\" uint32_t CSharpHostBindingLayoutType(uint32_t interfaceId, uint32_t typeIndex,\n";
        header += "                                                   char* name, uint32_t nameCap,\n";
        header += "                                                   uint64_t* sizeBytes, uint32_t* fieldCount);\n";
        header += "extern \"C\" uint32_t CSharpHostBindingLayoutField(uint32_t interfaceId, uint32_t typeIndex,\n";
        header += "                                                    uint32_t fieldIndex, char* name,\n";
        header += "                                                    uint32_t nameCap, char* typeName,\n";
        header += "                                                    uint32_t typeCap, uint64_t* offsetBytes);\n\n";
        header += "namespace ca_bind_registry\n{\n";
        header += "    uint32_t GetInterfaceApi(uint32_t interfaceId, char const* instanceName, uint32_t version,\n";
        header += "                             uint32_t size, void* out, void** instanceOut);\n";
        header += "    uint32_t GetBindingLayoutType(uint32_t interfaceId, uint32_t typeIndex, char* name,\n";
        header += "                                  uint32_t nameCap, uint64_t* sizeBytes, uint32_t* fieldCount);\n";
        header += "    uint32_t GetBindingLayoutField(uint32_t interfaceId, uint32_t typeIndex, uint32_t fieldIndex,\n";
        header += "                                   char* name, uint32_t nameCap, char* typeName, uint32_t typeCap,\n";
        header += "                                   uint64_t* offsetBytes);\n";
        header += "    // 装配：先让宿主建默认实例，再按宿主角色登记表填 EngineApi。\n";
        header += "    void FillEngineApi(EngineApi& api);\n";
        header += "}\n";

        std::string cpp;
        cpp += "// 由 CodeGen --emit-registry 生成，勿手改。\n";
        cpp += "#pragma once\n#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n";
        cpp += "#include \"BindingRegistry.Bindings.h\"\n";
        cpp += "// 逐接口产物：路由直接调它们生成的 GetApi / 布局查询入口。\n";
        for (RegistryEntry const& entry : entries)
        {
            cpp += "#include \"" + qualifiedLinearName(entry.qualifiedName) + ".Bindings.cpp\"\n";
        }
        cpp += "\nnamespace ca_bind_registry\n{\n";
        cpp += "    enum class BindingRole : uint32_t\n    {\n";
        cpp += "        Unregistered = 0,   // 宿主角色登记表里没有这个编号 —— 取表必须拒绝\n";
        cpp += "        DefaultInstance = 1, // 必须有默认实例\n";
        cpp += "        TargetTypeTable = 2, // 目标类型表：允许空实例\n";
        cpp += "    };\n\n";
        cpp += "    struct BindingRoute\n    {\n";
        cpp += "        uint32_t id;\n";
        cpp += "        char const* name;\n";
        cpp += "        uint32_t tableSize;\n";
        cpp += "        uint32_t (*getApi)(uint32_t version, uint32_t size, void* out);\n";
        cpp += "        uint32_t (*layoutType)(uint32_t typeIndex, char* name, uint32_t nameCap, uint64_t* sizeBytes,\n";
        cpp += "                               uint32_t* fieldCount);\n";
        cpp += "        uint32_t (*layoutField)(uint32_t typeIndex, uint32_t fieldIndex, char* name, uint32_t nameCap,\n";
        cpp += "                                char* typeName, uint32_t typeCap, uint64_t* offsetBytes);\n";
        cpp += "        BindingRole role;\n";
        cpp += "    };\n\n";
        for (RegistryEntry const& entry : entries)
        {
            const std::string linear = qualifiedLinearName(entry.qualifiedName);
            cpp += "    static uint32_t Route_" + linear + "(uint32_t version, uint32_t size, void* out)\n    {\n";
            cpp += "        return " + linear + "__GetApi(version, size, static_cast<" + linear +
                   "Api_v2*>(out));\n    }\n";
        }
        cpp += "\n    static BindingRoute kRoutes[] = {\n";
        for (RegistryEntry const& entry : entries)
        {
            const std::string linear = qualifiedLinearName(entry.qualifiedName);
            cpp += "        { " + std::to_string(entry.number) + "u, \"" + entry.shortName + "\",\n";
            cpp += "          static_cast<uint32_t>(sizeof(" + linear + "Api_v2)),\n";
            cpp += "          &Route_" + linear + ", &" + linear + "__GetLayoutType,\n";
            cpp += "          &" + linear + "__GetLayoutField, BindingRole::Unregistered },\n";
        }
        cpp += "    };\n";
        cpp += "    static char const* const kKnownInterfacesText = \"";
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (i != 0)
            {
                cpp += "、";
            }
            cpp += std::to_string(entries[i].number) + " = " + entries[i].qualifiedName;
        }
        cpp += "\";\n\n";
        cpp += "    static BindingRoute const* FindRoute(uint32_t interfaceId)\n    {\n";
        cpp += "        for (BindingRoute const& route : kRoutes)\n        {\n";
        cpp += "            if (route.id == interfaceId) { return &route; }\n        }\n";
        cpp += "        return nullptr;\n    }\n\n";
        cpp += "    static void Report(char const* text)\n    {\n";
        cpp += "        CSharpHostBindingReportError(text);\n    }\n\n";
        cpp += "    // 宿主角色登记（任务 3.1）：每张表在装配时问一次宿主，缺登记的编号保持 Unregistered，\n";
        cpp += "    // 取表时明确拒绝并点名 —— 不许静默按目标类型表放行。\n";
        cpp += "    static void ApplyPolicies()\n    {\n        for (BindingRoute& route : kRoutes)\n        {\n";
        cpp += "            // 注意：`requires` 是 C++20 关键字，变量名不能叫它（实测 MSVC 报 C2059）。\n";
        cpp += "            const int32_t requiresInstance = CSharpHostBindingRequiresInstance(route.id);\n";
        cpp += "            route.role = requiresInstance < 0 ? BindingRole::Unregistered\n";
        cpp += "                         : (requiresInstance != 0 ? BindingRole::DefaultInstance : BindingRole::TargetTypeTable);\n";
        cpp += "        }\n    }\n\n";
        cpp += "    uint32_t GetInterfaceApi(uint32_t interfaceId, char const* instanceName, uint32_t version,\n";
        cpp += "                             uint32_t size, void* out, void** instanceOut)\n    {\n";
        cpp += "        BindingRoute const* route = FindRoute(interfaceId);\n";
        cpp += "        if (route == nullptr)\n        {\n";
        cpp += "            char text[512];\n";
        cpp += "            ::snprintf(text, sizeof(text), \"未知接口编号 %u：当前登记 %zu 个接口（%s）\",\n";
        cpp += "                       interfaceId, sizeof(kRoutes) / sizeof(kRoutes[0]), kKnownInterfacesText);\n";
        cpp += "            Report(text);\n            return 0u;\n        }\n";
        cpp += "        if (route->role == BindingRole::Unregistered)\n        {\n";
        cpp += "            char text[512];\n";
        cpp += "            ::snprintf(text, sizeof(text),\n";
        cpp += "                       \"接口 %s（编号 %u）没有在宿主角色登记表里登记（默认实例 / 目标类型表必须二选一）\",\n";
        cpp += "                       route->name, interfaceId);\n";
        cpp += "            Report(text);\n            return 0u;\n        }\n";
        cpp += "        // 默认实例必须在**探针调用之前**解析：缺少实例时直接回 0，让托管侧按自己的\n";
        cpp += "        // `s_renderBindError` 点名接口并让探针明确失败；**不写宿主 LastError** ——\n";
        cpp += "        // 未启用渲染探针的既有窗口路径要求 LastError 保持为空（spec csharp-host）。\n";
        cpp += "        void* instance = nullptr;\n";
        cpp += "        if (route->role == BindingRole::DefaultInstance)\n        {\n";
        cpp += "            instance = CSharpHostBindingLookupInstance(interfaceId, instanceName);\n";
        cpp += "            if (instance == nullptr) { return 0u; }\n";
        cpp += "        }\n";
        cpp += "        // `size == 0` 是探针调用：只回真实表大小，不写任何内存。\n";
        cpp += "        if (size == 0u) { return route->tableSize; }\n";
        cpp += "        if (out == nullptr) { return 0u; }\n";
        cpp += "        if (instanceOut != nullptr) { *instanceOut = instance; }\n";
        cpp += "        return route->getApi(version, size, out);\n    }\n\n";
        cpp += "    uint32_t GetBindingLayoutType(uint32_t interfaceId, uint32_t typeIndex, char* name,\n";
        cpp += "                                  uint32_t nameCap, uint64_t* sizeBytes, uint32_t* fieldCount)\n    {\n";
        cpp += "        BindingRoute const* route = FindRoute(interfaceId);\n";
        cpp += "        if (route == nullptr || route->layoutType == nullptr) { return 0u; }\n";
        cpp += "        return route->layoutType(typeIndex, name, nameCap, sizeBytes, fieldCount);\n    }\n\n";
        cpp += "    uint32_t GetBindingLayoutField(uint32_t interfaceId, uint32_t typeIndex, uint32_t fieldIndex,\n";
        cpp += "                                   char* name, uint32_t nameCap, char* typeName, uint32_t typeCap,\n";
        cpp += "                                   uint64_t* offsetBytes)\n    {\n";
        cpp += "        BindingRoute const* route = FindRoute(interfaceId);\n";
        cpp += "        if (route == nullptr || route->layoutField == nullptr) { return 0u; }\n";
        cpp += "        return route->layoutField(typeIndex, fieldIndex, name, nameCap, typeName, typeCap, offsetBytes);\n";
        cpp += "    }\n\n";
        cpp += "    void FillEngineApi(EngineApi& api)\n    {\n";
        cpp += "        // 工厂由宿主按固定名字暴露（任务 3.1）：装配代码只负责按约定调用它。\n";
        cpp += "        CSharpHostBindingCreateDefaultInstances();\n";
        cpp += "        ApplyPolicies();\n";
        for (EngineApiFieldSpec const& field : kEngineApiFields)
        {
            const std::string cppName = field.cppName;
            if (field.hostSymbol != nullptr)
            {
                cpp += "        api." + cppName + " = &" + std::string(field.hostSymbol) + ";\n";
            }
            else if (std::string(cppName) == "abi_version")
            {
                cpp += "        api.abi_version = kEngineApiVersion;\n";
            }
            else if (std::string(cppName) == "size")
            {
                cpp += "        api.size = static_cast<uint32_t>(sizeof(EngineApi));\n";
            }
            else if (std::string(cppName) == "get_interface_api")
            {
                cpp += "        api.get_interface_api = &GetInterfaceApi;\n";
            }
            else if (std::string(cppName) == "get_binding_layout_type")
            {
                cpp += "        api.get_binding_layout_type = &GetBindingLayoutType;\n";
            }
            else if (std::string(cppName) == "get_binding_layout_field")
            {
                cpp += "        api.get_binding_layout_field = &GetBindingLayoutField;\n";
            }
            else if (field.interfaceShort != nullptr)
            {
                auto found = numberByShort.find(field.interfaceShort);
                if (found != numberByShort.end())
                {
                    auto qualified = qualifiedByShort.find(field.interfaceShort);
                    const std::string linear = qualified == qualifiedByShort.end()
                                                   ? std::string(field.interfaceShort)
                                                   : qualifiedLinearName(qualified->second);
                    cpp += "        api." + cppName + " = kBindingInterfaceId" + linear + ";\n";
                }
                else
                {
                    cpp += "        api." + cppName + " = 0u;  // 编号尚未登记（接口 " +
                           std::string(field.interfaceShort) + " 不在本次登记输入里）\n";
                }
            }
        }
        cpp += "    }\n}\n";

        std::string cs;
        cs += "// 由 CodeGen --emit-registry 生成，勿手改。\n";
        cs += "// 与 C++ 侧 BindingRegistry.Bindings.h 的 EngineApi 逐字段对照（同一次生成）。\n";
        cs += "using System.Runtime.InteropServices;\n\n";
        cs += "namespace Scripting.Managed\n{\n";
        cs += "    /// <summary>引擎交给托管侧的函数表。字段顺序与宽度就是 ABI —— 只许在末尾追加。</summary>\n";
        cs += "    [StructLayout(LayoutKind.Sequential)]\n";
        cs += "    internal unsafe struct EngineApiMirror\n    {\n";
        for (EngineApiFieldSpec const& field : kEngineApiFields)
        {
            cs += "        public " + std::string(field.csType) + " " + field.csName + ";\n";
        }
        cs += "    }\n\n";
        cs += "    /// <summary>生成的 ABI 常量与接口编号清单（Host.cs 只读这里，不再手抄）。</summary>\n";
        cs += "    internal static unsafe class EngineApiBindings\n    {\n";
        cs += "        public const uint AbiVersion = 12;\n";
        cs += "        public const uint Size = 56;\n";
        cs += "        public const uint TableVersion = 2;\n\n";
        for (RegistryEntry const& entry : entries)
        {
            cs += "        // " + entry.qualifiedName + "\n";
            cs += "        public const uint " +
                  interfaceConstantSuffix(entry.qualifiedName, entry.shortName) + " = " +
                  std::to_string(entry.number) + ";\n";
        }
        cs += "\n        internal delegate void InterfaceTableBind(byte* table);\n\n";
        cs += "        internal sealed class InterfaceTable\n        {\n";
        cs += "            public readonly uint Id;\n";
        cs += "            public readonly string Name;\n";
        cs += "            public readonly InterfaceTableBind Bind;\n";
        cs += "            public InterfaceTable(uint id, string name, InterfaceTableBind bind)\n";
        cs += "            {\n                Id = id;\n                Name = name;\n                Bind = bind;\n";
        cs += "            }\n        }\n\n";
        cs += "        internal static readonly InterfaceTable[] Tables = new InterfaceTable[]\n        {\n";
        for (RegistryEntry const& entry : entries)
        {
            cs += "            new InterfaceTable(" +
                  interfaceConstantSuffix(entry.qualifiedName, entry.shortName) + ", \"" +
                  entry.qualifiedName + "\", global::" + cSharpFullNameOf(entry.qualifiedName) +
                  "Bindings.Bind),\n";
        }
        cs += "        };\n";
        cs += "    }\n}\n";

        const std::string headerPath = outDir + "/BindingRegistry.Bindings.h";
        const std::string cppPath = outDir + "/BindingRegistry.Bindings.cpp";
        const std::string csPath = outDir + "/EngineApi.Bindings.cs";
        std::ofstream headerOut(headerPath, std::ios::binary);
        headerOut << header;
        if (!headerOut)
        {
            std::fprintf(stderr, "CodeGen: cannot write '%s'\n", headerPath.c_str());
            return 2;
        }
        std::ofstream cppOut(cppPath, std::ios::binary);
        cppOut << cpp;
        if (!cppOut)
        {
            std::fprintf(stderr, "CodeGen: cannot write '%s'\n", cppPath.c_str());
            return 2;
        }
        std::ofstream csOut(csPath, std::ios::binary);
        csOut << cs;
        if (!csOut)
        {
            std::fprintf(stderr, "CodeGen: cannot write '%s'\n", csPath.c_str());
            return 2;
        }
        std::string numbers;
        for (RegistryEntry const& entry : entries)
        {
            if (!numbers.empty())
            {
                numbers += "、";
            }
            numbers += std::to_string(entry.number) + " = " + entry.shortName;
        }
        std::fprintf(stderr, "登记：接口 %zu 个（%s）\n", entries.size(), numbers.c_str());
        std::fprintf(stdout, "CodeGen wrote: %s\n", headerPath.c_str());
        std::fprintf(stdout, "CodeGen wrote: %s\n", cppPath.c_str());
        std::fprintf(stdout, "CodeGen wrote: %s\n", csPath.c_str());
        return 0;
    }
}

int main(int argc, char** argv)
{
    // `--emit-registry`（render-interface-merge 任务 1.3）：不给头文件，只把前面
    // `--emit-binding --registry-part` 写下的分片汇总成跨接口登记产物。
    if (argc >= 2 && std::string(argv[1]) == "--emit-registry")
    {
        std::string registryOutDir;
        std::vector<std::string> partFiles;
        std::string registryIdLock;
        std::string registryIdScope;
        for (int i = 2; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--out-dir" && i + 1 < argc)
            {
                registryOutDir = argv[++i];
            }
            else if (a.rfind("--out-dir=", 0) == 0)
            {
                registryOutDir = a.substr(10);
            }
            else if (a == "--registry-part-file" && i + 1 < argc)
            {
                partFiles.push_back(argv[++i]);
            }
            else if (a.rfind("--registry-part-file=", 0) == 0)
            {
                partFiles.push_back(a.substr(20));
            }
            else if (a == "--id-lock" && i + 1 < argc)
            {
                registryIdLock = argv[++i];
            }
            else if (a.rfind("--id-lock=", 0) == 0)
            {
                registryIdLock = a.substr(10);
            }
            else if (a == "--id-scope" && i + 1 < argc)
            {
                registryIdScope = argv[++i];
            }
            else if (a.rfind("--id-scope=", 0) == 0)
            {
                registryIdScope = a.substr(11);
            }
            else
            {
                std::fprintf(stderr, "CodeGen: --emit-registry 不认识参数 '%s'\n", a.c_str());
                return 2;
            }
        }
        if (registryOutDir.empty())
        {
            std::fprintf(stderr, "CodeGen: --emit-registry needs a non-empty --out-dir.\n");
            return 2;
        }
        const int registryResult =
            bindemit::emitRegistry(registryOutDir, partFiles, registryIdLock, registryIdScope);
        std::fprintf(stderr, "CodeGen: --emit-registry 返回 %d\n", registryResult);
        return registryResult;
    }

    if (argc < 3)
    {
        std::fprintf(stderr,
            "usage: CodeGen <file.h> <TypeName> [--emit-reflection] [--qualified-name=<qn>] [-o <out>] [clang-arg ...]\n"
            "       CodeGen <file.h> <TypeName> --emit-all --out-dir <dir> [--qualified-name=<qn>] [clang-arg ...]\n"
            "       CodeGen <file.h> --emit-binding --out-dir <dir> [--id-lock <锁> --id-scope <段>] [clang-arg ...]\n"
            "       CodeGen <file.h> --update-ids --lock <锁> --scope <段> [clang-arg ...]\n"
            "       --emit-all writes ONE file per type: <dir>/<TypeName>.codegen.h (reflection + serialization + hash).\n");
        return 2;
    }

    // `--parse-report`（任务 4.1）：只解析、不产物；对真实头出"采集 / 排除"投影快照。
    if (std::string(argv[2]) == "--parse-report")
    {
        std::string outPath;
        std::vector<const char*> clangArgs;
        for (int i = 3; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--out" && i + 1 < argc)
            {
                outPath = argv[++i];
            }
            else if (a.rfind("--out=", 0) == 0)
            {
                outPath = a.substr(6);
            }
            else
            {
                clangArgs.push_back(argv[i]);
            }
        }
        return bindemit::parseReport(argv[1], clangArgs, outPath);
    }

    // `--update-ids`（本 change 任务 1.2）：唯一的发号入口。只加 / 只删锁里的行，既有行一字不动，
    // 不产任何绑定产物；名字仍由源码里的 CA_BIND 标注声明。
    if (std::string(argv[2]) == "--update-ids")
    {
        std::string lockPath;
        std::string scopeName;
        std::vector<const char*> clangArgs;
        for (int i = 3; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--lock" && i + 1 < argc)
            {
                lockPath = argv[++i];
            }
            else if (a.rfind("--lock=", 0) == 0)
            {
                lockPath = a.substr(7);
            }
            else if (a == "--scope" && i + 1 < argc)
            {
                scopeName = argv[++i];
            }
            else if (a.rfind("--scope=", 0) == 0)
            {
                scopeName = a.substr(8);
            }
            else
            {
                clangArgs.push_back(argv[i]);
            }
        }
        if (lockPath.empty() || scopeName.empty())
        {
            std::fprintf(stderr,
                         "CodeGen: --update-ids 需要 --lock <锁文件> 与 --scope <host|fixture>\n");
            return 2;
        }
        return bindemit::updateInterfaceIds(argv[1], lockPath, scopeName, clangArgs);
    }

    const char* filePath = argv[1];
    const char* typeName = argv[2];

    // codegen flags (pulled out of the clang-arg list so they are never fed to libclang).
    bool        emitReflection    = false;
    bool        emitSerialization = false;
    bool        emitHash          = false;
    // `--emit-all` asks for all three SECTIONS out of ONE parse, written into one `<Type>.codegen.h`. The
    // three flags above stay the way to ask for exactly one section; the two forms are mutually exclusive
    // (see the check below).
    bool        emitAll           = false;
    // `--emit-binding`：绑定发射器（一次处理整个头里所有 `CA_BIND` 接口，不需要 <TypeName> 参数）。
    bool        emitBinding       = false;
    // 绑定模式不需要 <TypeName>，所以 `--emit-binding` 通常落在 argv[2] 上，而下面的参数循环从 argv[3]
    // 开始 —— 不在这里先认一次，那个标志会被当成类型名吃掉，模式永远进不去（实测：报出的是
    // "--out-dir only applies together with --emit-all" 这条无关的守卫）。
    if (std::string(typeName) == "--emit-binding")
    {
        emitBinding = true;
    }
    // `--list-bind-interfaces`（任务 3.1）：同样不需要 <TypeName>，标志可能落在 argv[2] 上。
    bool listBindInterfaceMode = false;
    if (std::string(typeName) == "--list-bind-interfaces")
    {
        listBindInterfaceMode = true;
    }
    std::vector<std::pair<std::string, std::string>> knownInterfaceArgs;
    auto addKnownInterface = [&knownInterfaceArgs](std::string const& text) {
        const std::size_t split = text.find('=');
        if (split == std::string::npos || split == 0 || split + 1 >= text.size())
        {
            std::fprintf(stderr, "CodeGen: --known-interface 需要 <短名>=<完整限定名>（收到 '%s'）\n",
                         text.c_str());
            return;
        }
        const std::string shortName = text.substr(0, split);
        const std::string qualified = text.substr(split + 1);
        // 本批 1.1：同一个短名可以对应多条完整限定名（跨命名空间同名），只去重完全相同的条目。
        for (auto const& entry : knownInterfaceArgs)
        {
            if (entry.first == shortName && entry.second == qualified)
            {
                return;
            }
        }
        knownInterfaceArgs.emplace_back(shortName, qualified);
    };
    const char* outPath           = nullptr;
    const char* outDir            = nullptr;
    std::string qualifiedNameOverride;
    std::string auditScript;   // --audit-script：审计脚本路径（默认取头文件同目录）
    // 登记表输入（render-interface-merge 任务 1.1）：标签非空 = 本头是登记表发射输入。
    std::string registryPartLabel;
    std::string registryPartDir;
    // 编号锁（本 change 任务 1.2）：登记表输入的编号从锁文件读，段名区分宿主 / 夹具两个号空间。
    std::string idLockPath;
    std::string idScope;
    // 句柄位宽（任务 2.9）：序号位默认 48、代际位默认 16，合法范围由"两者之和不超过 64"约束
    unsigned    handleIndexBits  = 48u;
    unsigned    handleGenerationBits = 16u;
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
        else if (a == "--emit-all")
        {
            emitAll = true;
        }
        else if (a == "--emit-binding")
        {
            emitBinding = true;
        }
        else if (a == "--list-bind-interfaces")
        {
            listBindInterfaceMode = true;
        }
        else if (a == "--known-interface")
        {
            if (i + 1 < argc)
            {
                addKnownInterface(argv[++i]);
            }
        }
        else if (a.rfind("--known-interface=", 0) == 0)
        {
            addKnownInterface(a.substr(18));
        }
        else if (a == "--handle-index-bits")
        {
            if (i + 1 < argc) handleIndexBits = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        }
        else if (a == "--handle-generation-bits")
        {
            if (i + 1 < argc) handleGenerationBits = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        }
        else if (a == "--audit-script")
        {
            if (i + 1 < argc)
                auditScript = argv[++i];
        }
        // 登记表输入（render-interface-merge 任务 1.1/1.2）：本头写一份 `编号 → 接口` 分片，
        // 供随后的 `--emit-registry` 汇总。标签只用于分片文件名（ASCII、无路径分隔符）。
        else if (a == "--registry-part")
        {
            if (i + 1 < argc)
                registryPartLabel = argv[++i];
        }
        else if (a == "--registry-part-dir")
        {
            if (i + 1 < argc)
                registryPartDir = argv[++i];
        }
        else if (a == "--id-lock")
        {
            if (i + 1 < argc)
                idLockPath = argv[++i];
        }
        else if (a.rfind("--id-lock=", 0) == 0)
        {
            idLockPath = a.substr(10);
        }
        else if (a == "--id-scope")
        {
            if (i + 1 < argc)
                idScope = argv[++i];
        }
        else if (a.rfind("--id-scope=", 0) == 0)
        {
            idScope = a.substr(11);
        }
        else if (a == "-o")
        {
            if (i + 1 < argc)
                outPath = argv[++i];
        }
        else if (a == "--out-dir")
        {
            if (i + 1 < argc)
                outDir = argv[++i];
        }
        else if (a.rfind("--out-dir=", 0) == 0)
        {
            outDir = argv[i] + 10;
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

    // 本批 1.1：同短名不再按短名拒绝——判定改到命名空间映射之后（见 bindemit::run 的映射后碰撞判据）。
    if (listBindInterfaceMode)
    {
        return bindemit::listBindInterfaces(filePath, clangArgs);
    }

    // 绑定发射器走自己的分支：它按 `CA_BIND` 扫描整个头、一次出多份产物，不需要 <TypeName> 参数。
    // 放在 `--emit-all` 的一致性检查之前，因为那几条检查只对反射/序列化/哈希三节有意义。
    if (emitBinding)
    {
        bindemit::g_knownInterfaces = knownInterfaceArgs;
        if (outDir == nullptr || *outDir == '\0')
        {
            std::fprintf(stderr, "CodeGen: --emit-binding needs a non-empty --out-dir.\n");
            return 2;
        }
        // 句柄位宽的合法性（任务 2.9）：两项都至少 1 位，且加起来不超过 64
        if (handleIndexBits < 1u || handleGenerationBits < 1u || handleIndexBits + handleGenerationBits > 64u)
        {
            std::fprintf(stderr,
                         "CodeGen: 句柄位宽非法（--handle-index-bits %u、--handle-generation-bits %u）："
                         "两项都至少 1 位，且和不超过 64\n",
                         handleIndexBits, handleGenerationBits);
            return 2;
        }
        const std::string effectivePartDir = registryPartDir.empty() ? std::string(outDir) : registryPartDir;
        const int bindingResult =
            bindemit::run(filePath, outDir, clangArgs, auditScript, handleIndexBits, handleGenerationBits,
                          registryPartLabel, effectivePartDir, idLockPath, idScope);
        std::fprintf(stderr, "CodeGen: --emit-binding 返回 %d\n", bindingResult);
        return bindingResult;
    }

    // Argument consistency for `--emit-all`, settled before any parse work (it costs seconds to parse and
    // nothing here needs the parse). `--emit-all` writes ONE file per type under `--out-dir`, so it cannot
    // be given a single `-o` path instead; the two ways of naming outputs must not be mixed.
    {
        const int explicitEmitters = (emitReflection ? 1 : 0) + (emitSerialization ? 1 : 0) + (emitHash ? 1 : 0);
        // `--out-dir=` 与 `-o ''` 给的是**非空指针的空串**：只判指针的话两道守卫都会放行，产物会被拼成
        // "/<Type>.codegen.h" 写进**当前盘符的根目录**（实测退出码还是 0，调用方完全看不出来）。
        // 空值一律按"缺值"处理，先于下面几条配对检查报出来，信息才说得清是哪一个参数空的。
        if (outDir != nullptr && *outDir == '\0')
        {
            std::fprintf(stderr, "CodeGen: --out-dir needs a non-empty directory name.\n");
            return 2;
        }
        if (outPath != nullptr && *outPath == '\0')
        {
            std::fprintf(stderr, "CodeGen: -o needs a non-empty output path.\n");
            return 2;
        }
        if (emitAll && explicitEmitters > 0)
        {
            std::fprintf(stderr,
                "CodeGen: refusing — --emit-all already asks for all three sections, so it cannot be "
                "combined with --emit-reflection / --emit-serialization / --emit-hash.\n");
            return 10;
        }
        if (emitAll && !outDir)
        {
            std::fprintf(stderr,
                "CodeGen: --emit-all needs --out-dir <dir> — it writes one file per type "
                "(<Type>.codegen.h, holding the reflection + serialization + hash sections), so a bare "
                "-o <file> would name only a single section.\n");
            return 2;
        }
        if (outDir && !emitAll)
        {
            std::fprintf(stderr,
                "CodeGen: --out-dir only applies together with --emit-all; use -o <file> for a single "
                "emitter.\n");
            return 2;
        }
        if (outDir && outPath)
        {
            std::fprintf(stderr, "CodeGen: --out-dir and -o are mutually exclusive.\n");
            return 2;
        }
        // Turn the three flags on so EVERY gate below (the non-public-member refusal, the `serializable`
        // marker check, total order, default constructor, bit-packed elements) applies exactly as it does
        // when the emitters are asked for one at a time. Expanding later would silently skip the gates that
        // sit between here and the emission tail.
        if (emitAll)
        {
            emitReflection = emitSerialization = emitHash = true;
        }
    }

    // whitelist gating ("标过了才生成"): only emit a type that carries a discovery
    // marker (CA_REFLECT / CA_CODEGEN_REGISTER). Empty whitelist => EXIT 0 + no file
    // (spec "空名单不阻断 build"); non-empty whitelist + unannotated target => refuse (negative).
    const std::string source = readFile(filePath);
    if (source.empty())
    {
        std::fprintf(stderr, "CodeGen: cannot read '%s'\n", filePath);
        return 2;
    }
    // + 9.55 + 9.57: two views of the source, both with comments and INACTIVE preprocessor
    // branches blanked; `codeOnly` additionally blanks the inside of string and char literals. A marker
    // named only inside a comment, a string or a disabled `#if` branch must not count as an annotation, a
    // stray parenthesis in any of them must not unbalance the marker scan, and a literal containing the
    // block-comment opener must not be able to delete a real annotation.
    // names defined on the command line count as defined for `#ifdef` / `#ifndef`.
    // the annotation question is answered AFTER the parse, from the preprocessor's own list of
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
    // MSVC 的 STL 在 clang 身份下会检查编译器版本（`yvals_core.h`：`__clang_major__ < 19` 就
    // `_EMIT_STL_ERROR(STL1000, ...)`），而本仓库 vendored 的 libclang 比它旧。这个开关是 STL 自己
    // 留的口子（`_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH`）：关掉那条检查，STL 头照常解析。
    // 本工具只**解析**、不编译、不链接，布局由模块的真实编译环境（/MDd + 迭代器调试等级）决定，
    // 所以这里放行是安全的；不放行的话，凡是含 STL 类型的头都解析不了。
    args.push_back("-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH");
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

    // In the three-products mode stdout carries nothing else, so the dependency list goes there: build.py
    // reads it to decide whether this manifest row may be skipped next time. Sorted and de-duplicated,
    // because the ledger compares the whole set and libclang's visit order is not part of any contract.
    if (outDir != nullptr)
    {
        std::vector<std::string> deps;
        InclusionCollector          collector{ &deps };
        clang_getInclusions(tu, collectInclusion, &collector);
        std::sort(deps.begin(), deps.end());
        deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
        for (const std::string& dep : deps)
            std::printf("CodeGen deps: %s\n", dep.c_str());
        std::fflush(stdout);
    }

    // WHICH TYPES ARE ANNOTATED comes from the preprocessor. collectAnnotations walks the
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
        // an EMPTY annotation list is only legitimate when the parse actually succeeded, so the
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
            "CodeGen: type '%s' is not annotated (use CA_REFLECT or CA_CODEGEN_REGISTER); refusing to extract\n",
            typeName);
        abortOut();
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 4; // negative: an unmarked struct in a header that HAS marked types is not extracted.
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
    bool foundInMain = false;
    bool foundDefinition = false;
    // 选择口径，按优先级：
    //   ① **主文件里的定义** —— 被解析的那份头才是"要出码的类型"所在的地方。同名类型在被 include 的
    //      头里很常见（实测：`result_type` 在 sha256 / komihash / STL 里都有），取第一个会取错，
    //      生成物里就会出现裸名字（`struct TypeDescriptor<result_type>`）而不是限定名；
    //   ② 主文件里的声明、被 include 的头里的定义（老行为：优先定义，避免前置声明被当成空类型）；
    //   ③ 其它。
    for (auto& pr : structs)
    {
        if (pr.first != typeName)
            continue;
        const bool isDefinition = clang_isCursorDefinition(pr.second) != 0;
        const bool inMain = cursorIsInMainFile(pr.second, filePath);
        const bool better = !found
                            || (inMain && !foundInMain)
                            || (inMain == foundInMain && isDefinition && !foundDefinition);
        if (better)
        {
            target          = pr.second;
            found           = true;
            foundInMain     = inMain;
            foundDefinition = isDefinition;
        }
        if (foundInMain && foundDefinition)
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
    // 诊断开关（`set CODEGEN_TRACE_TARGET=1`）：把"到底选中了哪个游标"打出来。
    // 同名类型在一个 TU 里可以有很多（`result_type` 在 sha256 / komihash / STL 里都有），
    // 出问题时唯一想知道的就是这一条 —— 选中的是哪个文件哪一行、算出来的限定名是什么。
    if (std::getenv("CODEGEN_TRACE_TARGET") != nullptr)
    {
        unsigned line = 0, column = 0, offset = 0;
        CXFile file = nullptr;
        clang_getFileLocation(clang_getCursorLocation(target), &file, &line, &column, &offset);
        std::string where = "(no file)";
        if (file != nullptr)
        {
            CXString fn = clang_getFileName(file);
            const char* c = clang_getCString(fn);
            if (c != nullptr) where = c;
            clang_disposeString(fn);
        }
        std::fprintf(stderr, "CodeGen trace: '%s' -> %s:%u  qualified='%s'  definition=%d\n",
                     typeName, where.c_str(), line,
                     buildQualifiedName(target).c_str(),
                     static_cast<int>(clang_isCursorDefinition(target)));
    }
    // (found by the round-32 adversarial review): preferring the definition cursor was not
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

    // fidelity gate — PROPERTY-based, never based on which flags were passed.
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
        // the scan itself lives in scanDiagnostics() (shared with the empty-annotation check
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
            {
                std::fprintf(stderr, "  %u error-level diagnostic(s) inside the parsed header itself:\n",
                             inHeader);
                for (const std::string& msg : scan.headerErrors)
                    std::fprintf(stderr, "    %s\n", msg.c_str());
            }
            if (scan.headerErrors.empty() && !scan.anyErrors.empty())
            {
                std::fprintf(stderr, "  前几条错误（不在被解析的头里，多半来自它 include 的文件）:\n");
                for (const std::string& msg : scan.anyErrors)
                    std::fprintf(stderr, "    %s\n", msg.c_str());
            }
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
    // the emitters must use. There is deliberately NO sort by offsetBytes here: the extracted
    // offset is produced by a config-sensitive parse (a degenerate env collapses every offset to 0), so
    // deriving the emission order from it would make the output depend on an unverified value — and with
    // all-zero offsets the old offset comparator was a no-op anyway (measured). Declaration order is
    // reproducible run to run, which is what the determinism oracle actually needs.

    // reflection / serialization emitter: the qualified name comes from the AST
    // (or an explicit --qualified-name override) — the CA_REFLECT marker has no type name, so the tool
    // derives it from where the type actually lives. Both share the per-field metadata.
    const std::string qname =
        qualifiedNameOverride.empty() ? buildQualifiedName(target) : qualifiedNameOverride;

    // refuse to EMIT for a type with non-public members on the outer-class channel. Emitting
    // would produce a header that names private members (offsetof on them) and therefore cannot compile
    // — the old behaviour emitted it anyway and the failure surfaced far away, as C2248 in some consumer
    // TU. Extraction (no --emit-*) above this point is untouched: printing the type table for a private
    // type is legitimate and stays exit 0.
    //
    // 例外：类里写了 `CA_CODEGEN_FRIEND(T)`（展开成四条 friend 声明，把生成的四个入口加成友元）时放行 ——
    // 那种情况下生成代码读私有成员是合法的，正是老系统 `CA_PRIVATE_REFLECTION` 的等价物。
    if ((emitSerialization || emitHash || emitReflection) && sink.sawNonPublic && !sink.sawCodegenFriend)
    {
        std::fprintf(stderr,
            "CodeGen: refusing to emit for '%s' — it has non-public members, and the generated code "
            "reads members by name from outside the class (an in-class marker does not change that "
            "access check). Make them public, or put `CA_CODEGEN_FRIEND(%s);` inside the class body "
            "(it friends the four generated entry points — the equivalent of the old "
            "`CA_PRIVATE_REFLECTION`).\n",
            // 第二个 `%s` 要的是**类型名**（宏里就写它）。原先这里传的是 libclang 的类型游标 `CXCursor`
            // （一个结构体）——变参传递会把它的头几字节当成 `char*`，实测打出来是个控制字符 `\x02`。
            qname.c_str(), typeName);
        abortOut();
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 5;
    }

    // exactly one emitter per run — either one specific flag, or `--emit-all` for all three out of a single
    // parse. Before this check existed, `--emit-serialization --emit-hash` emitted only the first one,
    // dropped the other and exited 0 (measured: 1203 B, no `custom_hash_trait`).
    // `emitAll` is excluded because it has already turned all three flags on above (and asking it together
    // with a specific flag was refused earlier) — counting those three here would refuse the mode itself.
    {
        const int emitterCount = (emitReflection ? 1 : 0) + (emitSerialization ? 1 : 0) + (emitHash ? 1 : 0);
        if (emitterCount > 1 && !emitAll)
        {
            std::fprintf(stderr,
                "CodeGen: refusing — %d emitter flags in one run; --emit-reflection / --emit-serialization "
                "/ --emit-hash are mutually exclusive per invocation. Invoke the tool once per emitter, or "
                "ask for all three at once with --emit-all --out-dir <dir>.\n",
                emitterCount);
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 10;
        }
    }

    // the `serializable` marker option is what the design says gates this emitter
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

    // (1): the canonical order of an unordered container needs `<` on the sorting side. A type
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

    // (4): the generated reader builds each key / element with `T t{}` before reading into it,
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

    // (2a): a member that IS a pointer (`int* p`, `shared_ptr<T> p`) is refused for the same reason, one
    // level up: the old runtime never wrote a pointer member (the payload silently lost it), while the
    // new runtime has no such skip, so the generated code would write the address itself - meaningless
    // across runs. Refuse instead, and name the member.
    if (emitSerialization || emitHash)
    {
        for (const Field& f : fields)
        {
            if (!(f.memberIsOpaque || f.memberIsManagedPointer))
                continue;
            std::fprintf(stderr,
                "CodeGen: refusing %s for '%s' — member '%s' is a %s pointer ('%s'): the old runtime does "
                "not write pointer members, so the stream would silently lose it, and writing the address "
                "itself means nothing after a restart. Store the value, or store a name / index into a "
                "table that is serialized alongside.\n",
                (emitSerialization ? "--emit-serialization" : "--emit-hash"),
                qname.c_str(), f.name.c_str(),
                (f.memberIsManagedPointer ? "managed" : "raw"), f.typeName.c_str());
            abortOut();
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return 15;
        }
    }

    // (2): a container whose element is a raw pointer, a managed pointer or a handle cannot be
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

    // (3): a duplicate-key container (`multimap` / `multiset` / the unordered variants) has no
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

    // / 4.1 (5): a bit-packed container (`vector<bool>`) satisfies the structural criterion
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
    // Which products this invocation writes. Without `--emit-all` that is exactly one, as before; with it,
    // ONE file per type out of a single parse: `<Type>.codegen.h` under `--out-dir`, holding the three
    // sections in a fixed order (reflection -> serialization -> hash). Each section comes out of the same
    // emit function single-emitter mode uses, so its bytes are unchanged; what the merged file does NOT
    // repeat is the per-file preamble (its own `#pragma once` and runtime `#include`) — the merged header
    // wrote those once, at the top.
    if (emitSerialization || emitHash || emitReflection)
    {
        if (outDir)
        {
            // 一个类型一份产物：三节写进同一个临时文件，最后一次性改名提交 —— 任何一节出问题都不会留下
            // "半套"产物让下一轮误当成完整的一份。
            const std::string finalPath  = std::string(outDir) + "/" + typeName + ".codegen.h";
            const std::string tmpProduct = finalPath + ".tmp";
            FILE*             sink       = std::fopen(tmpProduct.c_str(), "w");
            if (!sink)
            {
                std::fprintf(stderr, "CodeGen: cannot open output '%s'\n", tmpProduct.c_str());
                clang_disposeTranslationUnit(tu);
                clang_disposeIndex(index);
                return 2;
            }
            emitMergedPreamble(qname, sink);
            if (emitReflection)
            {
                emitTypeDescriptor(ef, qname, sink, /*merged=*/true);
                std::fprintf(sink, "\n");
            }
            if (emitSerialization)
            {
                emitCodeGenSerializer(ef, qname, sink, /*merged=*/true);
                std::fprintf(sink, "\n");
            }
            if (emitHash)
                emitHashTrait(ef, qname, sink, /*merged=*/true);

            // Same temp-then-rename rule as `-o`: the requested path is only replaced after the product
            // was written out in full. std::rename does not replace an existing destination on Windows,
            // so the destination is removed first.
            bool ok = (std::fclose(sink) == 0);
            if (ok)
            {
                std::remove(finalPath.c_str());
                ok = (std::rename(tmpProduct.c_str(), finalPath.c_str()) == 0);
            }
            if (ok)
            {
                // Tell the caller WHAT this run actually wrote, on stdout, next to the `CodeGen deps:`
                // lines. build.py needs it because the products live in a directory that the parsed
                // headers themselves #include from (see the include-path change): it can no longer
                // delete the old products before the run to detect "exited 0 but wrote nothing",
                // because that deletion is exactly what made the parse fail.
                std::printf("CodeGen wrote: %s\n", finalPath.c_str());
            }
            else
            {
                std::fprintf(stderr,
                    "CodeGen: could not move '%s' onto '%s' — the requested output was NOT written\n",
                    tmpProduct.c_str(), finalPath.c_str());
                std::remove(tmpProduct.c_str());
            }
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return ok ? 0 : 7;
        }
        // 单发射器（`-o <路径>` 或标准输出）：与合并之前逐字节相同。
        for (int k = 0; k < 3; ++k)
        {
            const bool wanted = (k == 0) ? emitReflection : (k == 1) ? emitSerialization : emitHash;
            if (!wanted)
                continue;
            if (k == 0)
                emitTypeDescriptor(ef, qname, out, /*merged=*/false);
            else if (k == 1)
                emitCodeGenSerializer(ef, qname, out, /*merged=*/false);
            else
                emitHashTrait(ef, qname, out, /*merged=*/false);
            if (!commitOut())
            {
                clang_disposeTranslationUnit(tu);
                clang_disposeIndex(index);
                return 7;
            }
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

    // carry CA_REFLECT marker options (from source text) into the output.
    const std::string options = scanMarkerOptions(bound, typeName);
    if (!options.empty())
        std::printf("# options: %s\n", options.c_str());

    abortOut(); // no --emit-*: the table went to stdout, so the output path stays untouched
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    return 0;
}
