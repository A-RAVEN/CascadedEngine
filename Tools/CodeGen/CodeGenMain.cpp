//
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
    };

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
    };

    // 已标记 CA_BIND 的接口名（`run` 里填）。`projectMethod` 与回调形状解析都要用它判
    // "这个裸指针指向的是不是已绑定接口"。
    std::set<std::string> g_bindableInterfaceNames;

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

    // 已知接口清单（任务 3.1）：`--known-interface <短名>=<完整限定名>`。
    // 它只用于类型投影与诊断，不决定产物 —— 产物仍只看主头里的 CA_BIND。
    std::map<std::string, std::string> g_knownInterfaces;

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
    };

    struct ClassCollector
    {
        std::vector<std::string> const* wanted = nullptr;
        std::vector<InterfaceShape>* sink = nullptr;
    };

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
        if (clang_getCursorKind(cursor) != CXCursor_CXXMethod ||
            clang_CXXMethod_isPureVirtual(cursor) == 0u)
        {
            return CXChildVisit_Continue;
        }

        MethodSignature method;
        method.name = toStd(clang_getCursorSpelling(cursor));
        if (method.name.empty() || method.name[0] == '~')
        {
            return CXChildVisit_Continue;
        }
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
            for (std::string const& wanted : *collector->wanted)
            {
                if (name == wanted)
                {
                    // 同一个接口可能被访问多次（前向声明 + 定义）：只收一次
                    bool alreadyCollected = false;
                    for (InterfaceShape const& existing : *collector->sink)
                    {
                        if (existing.name == name)
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
                    // 带命名空间的完整拼写（thunk 里 static_cast 要用它；只写短名在命名空间里编不过）。
                    // libclang 的类型拼写在这里只给短名，所以自己沿"语义父"把命名空间串起来。
                    {
                        std::string prefix;
                        for (CXCursor parent = clang_getCursorSemanticParent(cursor);
                             !clang_Cursor_isNull(parent) &&
                             clang_getCursorKind(parent) == CXCursor_Namespace;
                             parent = clang_getCursorSemanticParent(parent))
                        {
                            prefix = toStd(clang_getCursorSpelling(parent)) + "::" + prefix;
                        }
                        shape.qualifiedName = prefix + name;
                    }
                    MethodCollector methods;
                    methods.sink = &shape.methods;
                    clang_visitChildren(cursor, collectMethods, &methods);
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
        std::map<std::string, std::string> skipReasons;  // 方法 → 理由（空串 = 没写理由）
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
        bool pendingTemplate = false;
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
            if (line.find("CA_BIND(") != std::string::npos)
            {
                pendingBind = true;
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
            if (!pendingBind)
            {
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
                markers.push_back(std::move(marker));
            }
            pendingBind = false;
            pendingTemplate = false;
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
        cs += "namespace Scripting.Generated.Bindings\n{\n";
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
        cs += "namespace Scripting.Generated.Bindings\n{\n";
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
            *cpp += "extern \"C\" uint32_t " + interfaceName + "_GetLayoutTypeCount(void)\n{\n    return 0u;\n}\n";
            *cpp += "extern \"C\" uint32_t " + interfaceName +
                    "_GetLayoutType(uint32_t, char*, uint32_t, uint64_t*, uint32_t*)\n{\n    return 0u;\n}\n";
            *cpp += "extern \"C\" uint32_t " + interfaceName +
                    "_GetLayoutField(uint32_t, uint32_t, char*, uint32_t, char*, uint32_t, uint64_t*)\n{\n"
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
        *cpp += "extern \"C\" uint32_t " + interfaceName + "_GetLayoutTypeCount(void)\n{\n    return " +
                std::to_string(mirrors.size()) + "u;\n}\n";
        *cpp += "extern \"C\" uint32_t " + interfaceName +
                "_GetLayoutType(uint32_t index, char* name, uint32_t nameCap, uint64_t* sizeBytes, "
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
                "_GetLayoutField(uint32_t typeIndex, uint32_t fieldIndex, char* name, uint32_t nameCap, "
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
std::string* rejectReason, bool* needsTwoStep, bool* isHandleReturn, bool* isCallback = nullptr)
    {
        *needsTwoStep = false;
        *isHandleReturn = false;
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
                 hasPrefix(returnBare, "castl::vector"))
        {
            *returnType = "void"; // 两段式：由伴生函数拷出
            *needsTwoStep = true;
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

    int run(const char* filePath, const char* outDir, const std::vector<const char*>& clangArgs,
            const std::string& auditScriptPath, unsigned handleIndexBits, unsigned handleGenerationBits)
    {
        const std::string sourceText = readWholeFile(filePath);
        const std::vector<BoundMarker> markers = scanBoundMarkers(sourceText);
        std::vector<std::string> bound;
        bound.reserve(markers.size());
        for (BoundMarker const& marker : markers)
        {
            bound.push_back(marker.interfaceName);
        }
        if (bound.empty())
        {
            std::fprintf(stderr, "CodeGen: --emit-binding found no CA_BIND marker in '%s'.\n", filePath);
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
        // 接口名清单：`projectMethod` 用它判"返回的是不是另一个可绑定接口的裸指针"（借用句柄，
        // 任务 5.3 / design D9）。本 change 2.x/3.1：主头里的接口 + `--known-interface` 传入的
        // 其它生成输入（跨头文件目标），两边都按**完整限定名**登记。
        g_bindableInterfaceNames.clear();
        int interfaceNameConflicts = 0;
        for (InterfaceShape const& shape : shapes)
        {
            g_bindableInterfaceNames.insert(shape.name);
            auto known = g_knownInterfaces.find(shape.name);
            if (known != g_knownInterfaces.end() && known->second != shape.qualifiedName)
            {
                std::fprintf(stderr,
                             "CodeGen: 拒绝 —— 同短名接口 %s 有两种完整限定名：%s / %s"
                             "（--known-interface 清单与主头声明不一致）\n",
                             shape.name.c_str(), known->second.c_str(), shape.qualifiedName.c_str());
                ++interfaceNameConflicts;
            }
            g_knownInterfaces[shape.name] = shape.qualifiedName;
        }
        // 已知清单里的其它生成输入（跨头文件目标）也要参与类型投影：主头没有 include 它们，
        // 但方法签名里可能就写着它们的短名。
        for (auto const& entry : g_knownInterfaces)
        {
            g_bindableInterfaceNames.insert(entry.first);
        }
        if (interfaceNameConflicts > 0)
        {
            clang_disposeTranslationUnit(unit);
            clang_disposeIndex(index);
            return 1;
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
            for (InterfaceShape const& shape : shapes)
            {
                std::set<std::string> wantedMirrors;
                auto noteType = [&wantedMirrors](std::string const& projectedType) {
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
                        wantedMirrors.insert(bare.substr(0, bare.size() - 1));
                    }
                };
                for (MethodSignature const& method : shape.methods)
                {
                    std::vector<Projected> params;
                    std::string returnType;
                    std::string rejectReason;
                    bool needsTwoStep = false;
                    bool isHandleReturn = false;
                    if (!projectMethod(method, &params, &returnType, &rejectReason, &needsTwoStep, &isHandleReturn))
                    {
                        continue;
                    }
                    noteType(returnType);
                    for (Projected const& projected : params)
                    {
                        noteType(projected.type);
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
            unboundCollector.bound = &bound;
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
                std::string key = method.name + "|" + returnType + "(";
                for (Projected const& projected : params)
                {
                    key += projected.type + ",";
                }
                key += ")";
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
        for (InterfaceShape const& shape : shapes)
        {
            std::string cpp;
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
            cs += "namespace Scripting.Generated.Bindings\n{\n";
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
            cpp += "extern \"C\" int32_t " + shape.name + "_LastErrorCode(void* self);\n";
            cpp += "extern \"C\" int32_t " + shape.name + "_LastErrorText(void* self, char* dst, uint64_t cap);\n";
            // 句柄的解析/释放也要进表（任务 5.2）：托管表面的 `Dispose` 与"解出 self"都靠它 ——
            // 这两个入口不在表里，托管侧连函数地址都拿不到（它啃不动 DLL 里的导出符号名）。
            cpp += "extern \"C\" void* " + shape.name + "_ResolveHandle(void* self, uint64_t handle);\n";
            cpp += "extern \"C\" void " + shape.name + "_ReleaseHandle(void* self, uint64_t handle);\n";
            // 目标接口入口：追加在函数表末尾（任务 3.2），ABI 版本不变。
            cpp += "extern \"C\" void* " + shape.name + "_ResolveTargetHandle(void* self, uint64_t handle);\n";
            cpp += "extern \"C\" void " + shape.name + "_ReleaseTargetHandle(void* self, uint64_t handle);\n";
            // 布局清单（任务 2.5）：镜像结构体 + 编译期断言。必须**在方法声明的循环之前**发出 ——
            // 声明里会用到镜像类型，放在后面就是"先用后定义"（实测 C4430 / C2378）。
            emitLayoutManifest(&cpp, &cs, shape.name, mirrors);
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
                                  "&" + shape.name + "_readErrorCode", "void*, int"});
            tableSlots.push_back({"LastErrorText", "void (*LastErrorText)(void* self, char* dst, uint64_t cap)",
                                  "&" + shape.name + "_readErrorText", "void*, byte*, ulong, void"});
            tableSlots.push_back({"ResolveHandle", "void* (*ResolveHandle)(void* self, uint64_t handle)",
                                  "&" + shape.name + "_ResolveHandle", "void*, ulong, void*"});
            tableSlots.push_back({"ReleaseHandle", "void (*ReleaseHandle)(void* self, uint64_t handle)",
                                  "&" + shape.name + "_ReleaseHandle", "void*, ulong, void"});
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
                const std::string& I = shape.name;
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
                                   "_ReportCallbackError(void* self, int32_t code, char const* text, uint64_t textLen)\n{\n";
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
                if (!projectMethod(method, &params, &returnType, &rejectReason, &needsTwoStep, &isHandleReturn,
                                   &isCallback))
                {
                    std::fprintf(stderr, "CodeGen: 拒绝 %s::%s —— %s\n", shape.name.c_str(),
                                 method.name.c_str(), rejectReason.c_str());
                    ++rejected;
                    continue;
                }
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
                    { &method, params, returnType, needsTwoStep, isHandleReturn, isCallback, callbackInfo });
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
                    cpp += "extern \"C\" uint64_t " + shape.name + "_" + method.name + "Len(void* self);\n";
                    cpp += "extern \"C\" void " + shape.name + "_Copy" + method.name +
                           "(void* self, char* dst, uint64_t cap);\n";
                    // 两个伴生**各自独立**取一次实现的值：不依赖跨调用的暂存状态
                    lateDefs += "extern \"C\" uint64_t " + shape.name + "_" + method.name +
                                "Len(void* self)\n{\n";
                    // 两个伴生也是"入口"：不先清就会把上一次调用的失败码算到这次头上
                    lateDefs += "    " + shape.name + "_clearError(self);\n";
                    lateDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    lateDefs += "    return static_cast<uint64_t>(object->" + method.name + "().size());\n}\n";
                    lateDefs += "extern \"C\" void " + shape.name + "_Copy" + method.name +
                                "(void* self, char* dst, uint64_t cap)\n{\n";
                    lateDefs += "    " + shape.name + "_clearError(self);\n";
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
                    tableSlots.push_back({method.name + "Len", "uint64_t (*" + method.name + "Len)(void* self)",
                                          "&" + shape.name + "_" + method.name + "Len", "void*, ulong"});
                    tableSlots.push_back({"Copy" + method.name,
                                          "void (*Copy" + method.name +
                                              ")(void* self, char* dst, uint64_t cap)",
                                          "&" + shape.name + "_Copy" + method.name,
                                          "void*, byte*, ulong, void"});
                    continue;
                }
                std::string signature = returnType + " " + shape.name + "_" + method.name + "(void* self";
                for (Projected const& projected : params)
                {
                    signature += ", " + projectedDeclaration(projected);
                }
                signature += ");";
                cpp += "extern \"C\" " + signature + "\n";

                // 两段式伴生：先问长度、再拷出来（矩阵里 string / string_view / 容器返回那一行）
                if (needsTwoStep)
                {
                    cpp += "extern \"C\" uint64_t " + shape.name + "_" + method.name + "Len(void* self);\n";
                    cpp += "extern \"C\" void " + shape.name + "_Copy" + method.name +
                           "(void* self, char* dst, uint64_t cap);\n";
                }
                // 句柄的配对释放（矩阵里智能指针返回那一行）：定义成查表 + 代际进位
                if (isHandleReturn)
                {
                    cpp += "extern \"C\" void " + shape.name + "_Release" + method.name +
                           "(void* self, uint64_t handle);\n";
                    // 配对释放也按 self 记错误码（任务 5.10）：释放失败要能归到调用它的那个实例
                    lateDefs += "extern \"C\" void " + shape.name + "_Release" + method.name +
                                "(void* self, uint64_t handle)\n{\n    " + shape.name +
                                "_clearError(self);\n    " + shape.name +
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
                tableSlots.push_back({method.name,
                                      returnType + " (*" + method.name + ")(" + paramTypes + ")",
                                      "&" + shape.name + "_thunk_" + method.name,
                                      csParamTypes + ", " + abiCSharpType(returnType)});

                // thunk 实现：先只覆盖"参数全是标量、返回是 void 或同名标量"的形状；
                // 其它形状（容器 / 两段式 / 句柄 / 回调 / 出参）留给 2.4 / 2.5，这里必须是可见的桩。
                thunkDefs += "static " + returnType + " " + shape.name + "_thunk_" + method.name + "(void* self";
                for (Projected const& projected : params)
                {
                    thunkDefs += ", " + projectedDeclaration(projected);
                }
                thunkDefs += ")\n{\n";
                // 每个 thunk 入口先清一次（任务 5.10）：读到 0 只可能表示"这次调用没出错"。
                // 上一次失败留下的码不会粘在这里 —— D8 的误抛就是粘出来的。
                thunkDefs += "    " + shape.name + "_clearError(self);\n";
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
                    // 字节入参：调用前重建 castl::vector<uint8_t>；空指针只允许配零长度。
                    if (isByteVectorSpelling(spelling))
                    {
                        const std::string bytesLocal = paramName + "Bytes";
                        nativePrelude += "    castl::vector<uint8_t> " + bytesLocal + ";\n";
                        nativePrelude += "    if (" + paramName + " == nullptr)\n    {\n";
                        nativePrelude += "        if (" + paramName + "Count != 0u)\n        {\n";
                        nativePrelude += "            " + shape.name + "_setError(self, " + shape.name +
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
                            nativePrelude += "    " + innerMirror->cppName + " " + valueLocal + " = " + shape.name +
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
                        auto known = g_knownInterfaces.find(innerShort);
                        const std::string targetLiteral = known == g_knownInterfaces.end() ? std::string()
                                                                                          : known->second;
                        // 原生实参类型必须用完整限定名：主头里可能只有短名（跨头文件目标）。
                        std::string qualifiedInner = inner;
                        if (known != g_knownInterfaces.end())
                        {
                            qualifiedInner = known->second;
                        }
                        else if (const MirrorType* innerMirror = findMirrorByCpp(bareType(inner)))
                        {
                            qualifiedInner = innerMirror->cppName;
                        }
                        const std::string strongLocal = paramName + "Strong";
                        const std::string pointerLocal = paramName + "Shared";
                        nativePrelude += "    std::shared_ptr<void> " + strongLocal + " = " + shape.name +
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
                        nativePrelude += "    " + paramMirror->cppName + " " + recordLocal + " = " + shape.name +
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
                                             shape.name + "_fromC_" + elementMirror->csName + "(" + paramName +
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
                auto targetLiteralOf = [](std::string const& typeSpelling) -> std::string {
                    const std::string shortName = unqualifiedName(bareType(typeSpelling));
                    auto known = g_knownInterfaces.find(shortName);
                    return known == g_knownInterfaces.end() ? std::string() : known->second;
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
                    thunkDefs += "        if (" + shape.name + "_callbackModifyBlocked(self, " + indexText +
                                 ")) { return 0u; }\n";
                    thunkDefs += "        " + shape.name + "_supersedeCallback(self, " + indexText + ");\n";
                    thunkDefs += "        try { object->" + method.name + "(castl::function<" + originalCallbackType +
                                 ">{}); }\n";
                    thunkDefs += "        catch (...) { " + shape.name + "_setError(self, " + shape.name +
                                 "_ERROR_CALLBACK_EXCEPTION, \"原生实现抛出了 C++ 异常\"); return 0u; }\n";
                    // 清空不产生注册号，挂起的回调错误可以在这里安全上报（安装成功的路径不 attach，
                    // 否则 C# 会因为异常而丢掉刚拿到的注册号，保活项与 native 注册就对不上了）。
                    thunkDefs += "        " + shape.name + "_attachCallbackError(self);\n";
                    thunkDefs += "        return 0u;\n    }\n";
                    thunkDefs += "    if (" + shape.name + "_callbackModifyBlocked(self, " + indexText +
                                 ")) { return 0u; }\n";
                    thunkDefs += "    const uint64_t registration = " + shape.name + "_registerCallback(self, " +
                                 indexText + ");\n";
                    thunkDefs += "    if (registration == 0u) { return 0u; }\n";
                    thunkDefs += "    auto callback = [registration, self, " + fnName + ", " + userName + "](" +
                                 lambdaParams + ") {\n";
                    thunkDefs += "        " + shape.name + "_beginCallback(registration);\n";
                    thunkDefs += "        try { " + fnName + "(" + userName + forwardArgs + "); }\n";
                    thunkDefs += "        catch (std::exception const& error) { " + shape.name +
                                 "_setCallbackError(self, " + shape.name +
                                 "_ERROR_CALLBACK_EXCEPTION, error.what()); }\n";
                    thunkDefs += "        catch (...) { " + shape.name + "_setCallbackError(self, " + shape.name +
                                 "_ERROR_CALLBACK_EXCEPTION, \"原生回调桥抛出了 C++ 异常\"); }\n";
                    thunkDefs += "        " + shape.name + "_endCallback(registration);\n    };\n";
                    thunkDefs += "    try { object->" + method.name + "(castl::function<" + originalCallbackType +
                                 ">(callback)); }\n";
                    thunkDefs += "    catch (...) { " + shape.name + "_unregisterCallback(self, registration); " +
                                 shape.name + "_setError(self, " + shape.name +
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
                        thunkDefs += "    return " + shape.name + "_registerWeakHandle(self, std::weak_ptr<void>(" +
                                     call + "), \"" + target + "\");\n";
                    }
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
                        thunkDefs += "    " + shape.name + "_attachCallbackError(self);\n";
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
                        thunkDefs += "    return " + shape.name + "_registerBorrowedHandle(self, " + borrowCall +
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
                        thunkDefs += "    if (!result) { " + shape.name + "_setError(self, " + shape.name +
                                     "_ERROR_NULL_POINTER, \"实现返回了空指针\"); return 0u; }\n";
                        thunkDefs += "    return " + shape.name +
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
                            thunkDefs += "    " + shape.name + "_attachCallbackError(self);\n";
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
                    thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    // 显示器编号校验（本 change 任务 3.2 / design D9）：必须在调原生实现**之前**拦下
                    // 负数与越界编号。原生 `GetMonitor` 用 `castl::clamp` 把无效编号折成另一个真实
                    // 显示器，放进去就再也分不清"调用成功"和"被静默改正"。
                    if (method.name == "GetMonitor" && !method.params.empty() &&
                        isScalarType(bareType(method.params[0].first)))
                    {
                        const std::string monitorName = method.params[0].second;
                        thunkDefs += "    if (" + monitorName + " < 0 || " + monitorName +
                                     " >= object->GetMonitorCount())\n    {\n";
                        thunkDefs += "        char monitorError[96] = {0};\n";
                        thunkDefs += "        ::snprintf(monitorError, sizeof(monitorError), \"无效的显示器编号 %d（数量 %d）\", " +
                                     monitorName + ", object->GetMonitorCount());\n";
                        thunkDefs += "        " + shape.name + "_setError(self, " + shape.name +
                                     "_ERROR_INVALID_MONITOR, monitorError);\n";
                        thunkDefs += "        return {};\n    }\n";
                    }
                    thunkDefs += nativePrelude;
                    std::string call = "object->" + method.name + "(";
                    for (std::size_t index = 0; index < nativeArgs.size(); ++index)
                    {
                        call += (index == 0 ? "" : ", ") + nativeArgs[index];
                    }
                    call += ")";
                    thunkDefs += "    " + qualify(mirrorReturn->cppName) + " result = " + call + ";\n";
                    thunkDefs += "    return " + shape.name + "_toC_" + mirrorReturn->csName + "(result);\n";
                }
                else
                {
                    // 未映射的组合不应该出现在产物里：projectMethod 已经在写产物前拒绝它。
                    thunkDefs += "    /* 未支持的形状组合（projectMethod 本应拒绝） */\n" + zeroReturn;
                }
                thunkDefs += "}\n\n";

                // 导出入口：`extern "C"` 的那一层只是把参数原样转给 thunk（声明在上面，定义在这里）
                externDefs += "extern \"C\" " + returnType + " " + shape.name + "_" + method.name +
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
                        externDefs += "    " + shape.name + "_thunk_" + method.name + "(" + callArguments +
                                      ");\n";
                    }
                    else
                    {
                        externDefs += "    return " + shape.name + "_thunk_" + method.name + "(" +
                                      callArguments + ");\n";
                    }
                }
                externDefs += "}\n\n";
            }
            if (hasCallbacks)
            {
                // 通用注销伴生（design D1）：按注册号清理仍匹配的那一项；旧注册号静默无操作，
                // 不许误删后来替换的新回调。它进函数表 —— 托管侧只有函数表地址，没有导出符号名。
                cpp += "extern \"C\" void " + shape.name + "_UnregisterCallback(void* self, uint64_t registration);\n";
                thunkDefs += "static void " + shape.name +
                             "_thunk_UnregisterCallback(void* self, uint64_t registration)\n{\n";
                thunkDefs += "    " + shape.name + "_clearError(self);\n";
                thunkDefs += "    " + shape.name + "_unregisterCallback(self, registration);\n";
                thunkDefs += "    " + shape.name + "_attachCallbackError(self);\n}\n\n";
                externDefs += "extern \"C\" void " + shape.name +
                              "_UnregisterCallback(void* self, uint64_t registration)\n{\n";
                externDefs += "    " + shape.name + "_thunk_UnregisterCallback(self, registration);\n}\n\n";
                tableSlots.push_back({"UnregisterCallback",
                                      "void (*UnregisterCallback)(void* self, uint64_t registration)",
                                      "&" + shape.name + "_thunk_UnregisterCallback", "void*, ulong, void"});
                // 托管桥把回调里捕获的异常 / 跨线程 / 重入错误写进回调错误槽的入口
                cpp += "extern \"C\" void " + shape.name +
                       "_ReportCallbackError(void* self, int32_t code, char const* text, uint64_t textLen);\n";
                tableSlots.push_back({"ReportCallbackError",
                                      "void (*ReportCallbackError)(void* self, int32_t code, char const* text, "
                                      "uint64_t textLen)",
                                      "&" + shape.name + "_ReportCallbackError",
                                      "void*, int, byte*, ulong, void"});
            }
            // 目标接口两个槽：**追加**在函数表末尾（任务 3.2）。不重排旧槽、ABI 版本不变。
            tableSlots.push_back({"ResolveTargetHandle",
                                  "void* (*ResolveTargetHandle)(void* self, uint64_t handle)",
                                  "&" + shape.name + "_ResolveTargetHandle", "void*, ulong, void*"});
            tableSlots.push_back({"ReleaseTargetHandle",
                                  "void (*ReleaseTargetHandle)(void* self, uint64_t handle)",
                                  "&" + shape.name + "_ReleaseTargetHandle", "void*, ulong, void"});
            cpp += "\n";
            // ---- 版本化函数表 + 取表函数（任务 2.2）----
            // 校验口径：**版本相等且大小相等**才通过（与已落地的 Scripting/Managed/Host.cs 一致）；
            // 不符时一个字节都不写、返回 0。`GetApi` 把调用方的 size 收进来，就是为了"只写它给的那一段"。
            // 布局清单已在本文件开头发出（镜像类型必须先于声明）
            // 版本从 1 升到 2（任务 5.10）：表头在 abi_version / size 之后多两个错误通道槽。
            // 两侧 `GetApi` 的"版本相等且大小相等"双校验因此自然拦住上一版产物 —— 不需要
            // 另加一条兼容判断，旧表拿到新版本号直接返回 0。
            cpp += "typedef struct " + shape.name + "Api_v2 {\n";
            cpp += "    uint32_t abi_version;\n";
            cpp += "    uint32_t size;\n";
            // 表头之后是四个非方法槽（错误通道两槽 + 句柄解析/释放两槽），再是方法与两段式伴生。
            // 顺序全靠 tableSlots，C++ 的 static_assert 与 C# 的偏移常量都从同一份列表算出来。
            for (TableSlot const& slot : tableSlots)
            {
                cpp += "    " + slot.member + ";\n";
            }
            cpp += "} " + shape.name + "Api_v2;\n\n";
            // 句柄表与错误通道的**数据**必须定义在 thunk 之前（thunk 里要用），实现体可以放在后面 ——
            // 这里先把类型、两张表与三个 helper 的前置声明吐出来，避免"先用后定义"。
            cpp += "static const uint32_t " + shape.name + "_HANDLE_INDEX_BITS = " + std::to_string(handleIndexBits) + "u;\n";
            cpp += "static const uint32_t " + shape.name + "_HANDLE_GEN_BITS = " + std::to_string(handleGenerationBits) + "u;\n";
            // 错误码一律具名（本 change 任务 4.3）：唯一来源是上面的 `kBindErrorCodes`，生成物里不许出现
            // 裸字面量；`check_bindings.py` 的 `ERROR_CODE_TABLE` 与 README 的错误码表跟它逐项对齐。
            for (BindErrorCode const& errorCode : kBindErrorCodes)
            {
                cpp += "static const int32_t " + shape.name + "_ERROR_" + errorCode.name + " = " +
                       std::to_string(errorCode.value) + ";\n";
            }
            // 错误状态按 **self（实例）** 分槽（任务 5.10 / design D11）：以前是每接口一份
            // static 全局，`LastErrorCode(void*)` 直接把 self 丢掉，于是两个实例会串台，
            // 而且一次失败之后紧接着一次成功的调用读到的仍是上一次的码（D8 会误抛）。
            // 键用 self 指针：每个模块实例、每个窗口对象的 self 都不同，接口这一维由
            // "每个接口一份生成物"本身保证，所以 `(接口, 实例)` 落下来就是 `(本文件, self)`。
            cpp += "typedef struct " + shape.name + "ErrorSlot { void const* self; int32_t code; char text[256]; } " +
                   shape.name + "ErrorSlot;\n";
            // 与句柄表同一口径：挂在永不释放的堆对象上，避开静态析构顺序（实测过崩溃）
            cpp += "static std::vector<" + shape.name + "ErrorSlot>& " + shape.name +
                   "_errorSlots = *new std::vector<" + shape.name + "ErrorSlot>();\n";
            cpp += "static std::recursive_mutex " + shape.name + "_errorMutex;\n";
            // 句柄表是**进程级唯一一张**（任务 5.11 / design D12 乙）：所有接口共用，表项带接口标签。
            // 为什么不能每接口一张：句柄 = (代际 << 48) | 序号，而每个接口的序号都从 1 开始，两张独立
            // 的表会发出**数字完全相同**的句柄；把 A 表的句柄交给 B 表的释放入口，B 表会命中一个同号
            // 的活项并把它清掉 —— 销毁的是一个无关对象（实测，口径见 tasks.md 5.11）。
            cpp += "// 本接口在共用句柄表里的标签：一个本翻译单元独有的静态对象地址，跨接口不会撞\n";
            cpp += "static int const " + shape.name + "_HANDLE_TAG = 0;\n";
            // 兼容既有读写点（CoreTests 直接读表项的 kind / strong）：类型换成共用的那个
            cpp += "static std::vector<cascaded_bindings::HandleEntry>& " + shape.name +
                   "_handles = cascaded_bindings::HandleTable();\n";
            const std::string handleMutexName = "cascaded_bindings::HandleMutex()";
            // 错误通道的转发声明必须排在 thunk 之前（thunk 入口第一句就是 clearError）。
            // 槽的键是 self：错误码只对"这次调用的那个实例"有意义。
            cpp += "static " + shape.name + "ErrorSlot& " + shape.name + "_errorSlotLocked(void const* self);\n";
            cpp += "static void " + shape.name + "_clearError(void const* self);\n";
            cpp += "static void " + shape.name + "_setError(void const* self, int32_t code, char const* text);\n";
            cpp += "static int32_t " + shape.name + "_readErrorCode(void* self);\n";
            cpp += "static void " + shape.name + "_readErrorText(void* self, char* dst, uint64_t cap);\n";
            cpp += "static uint64_t " + shape.name + "_registerStrongHandle(void const* owner, std::shared_ptr<void> object, char const* targetType);\n";
            cpp += "static uint64_t " + shape.name + "_registerWeakHandle(void const* owner, std::weak_ptr<void> object, char const* targetType);\n";
            cpp += "static uint64_t " + shape.name + "_registerBorrowedHandle(void const* issuer, void* object, char const* targetType);\n";
            cpp += "static std::shared_ptr<void> " + shape.name +
                   "_lookupStrongHandle(void const* owner, uint64_t handle, char const* expectedTarget, char const* roleText);\n";
            cpp += "static void* " + shape.name + "_lookupHandle(uint64_t handle);\n";
            cpp += "static void* " + shape.name +
                   "_lookupTargetHandle(void const* owner, uint64_t handle, char const* expectedTarget);\n";
            cpp += "static void " + shape.name + "_releaseHandle(void const* owner, uint64_t handle);\n";
            cpp += "static void " + shape.name +
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
                    cpp += "static_assert(offsetof(" + shape.name + "Api_v2, " + slot.name + ") == " +
                           std::to_string(tableHeaderBytes + 8u * memberIndex) +
                           ", \"函数表成员偏移变了：\" \"" + shape.name + "." + slot.name + "\");\n";
                    cs += "        public const int " + shape.name + "Api_v2_" + slot.name + " = " +
                          std::to_string(tableHeaderBytes + 8u * memberIndex) + ";\n";
                    ++memberIndex;
                }
                cpp += "\n";
            }
            cpp += "static " + shape.name + "Api_v2 const " + shape.name + "_api = {\n";
            cpp += "    2u, (uint32_t)sizeof(" + shape.name + "Api_v2),\n";
            // 四个非方法槽按 tableSlots 的顺序紧跟在表头后面
            for (TableSlot const& slot : tableSlots)
            {
                cpp += "    " + slot.init + ",\n";
            }
            cpp += "};\n\n";
            cpp += "extern \"C\" uint32_t " + shape.name + "_GetApi(uint32_t version, uint32_t size, " +
                   shape.name + "Api_v2* out)\n{\n";
            cpp += "    const uint32_t realSize = (uint32_t)sizeof(" + shape.name + "Api_v2);\n";
            cpp += "    if (version != 2u || size != realSize || out == nullptr)\n    {\n        return 0u;\n    }\n";
            cpp += "    ::memcpy(out, &" + shape.name + "_api, realSize);\n";
            cpp += "    return realSize;\n}\n\n";
            // ---- 错误通道 + 句柄表（任务 2.4）----
            // 句柄 = 低 <INDEX> 位序号 + 高 <GEN> 位代际（位数在 2.9 变成参数，这里先用默认 48/16）。
            // `Release` 让代际进位 ⇒ 旧句柄立刻失效；失效时置错误码 1001 并留一条文本。
            // ---- 错误状态：按 self 分槽（任务 5.10 / design D11）----
            // 每个槽一个自述文本（最多 255 字节 + NUL），`text` 里永远留一个 NUL。
            cpp += "static " + shape.name + "ErrorSlot& " + shape.name + "_errorSlotLocked(void const* self)\n{\n";
            cpp += "    for (auto& slot : " + shape.name + "_errorSlots)\n    {\n";
            cpp += "        if (slot.self == self) { return slot; }\n    }\n";
            cpp += "    " + shape.name + "_errorSlots.push_back({self, 0, {0}});\n";
            cpp += "    return " + shape.name + "_errorSlots.back();\n}\n";
            cpp += "static void " + shape.name + "_clearError(void const* self)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + shape.name + "_errorMutex);\n";
            cpp += "    " + shape.name + "ErrorSlot& slot = " + shape.name + "_errorSlotLocked(self);\n";
            cpp += "    slot.code = 0;\n    slot.text[0] = 0;\n}\n";
            cpp += "static void " + shape.name + "_setError(void const* self, int32_t code, char const* text)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + shape.name + "_errorMutex);\n";
            cpp += "    " + shape.name + "ErrorSlot& slot = " + shape.name + "_errorSlotLocked(self);\n";
            cpp += "    slot.code = code;\n    slot.text[0] = 0;\n";
            cpp += "    if (text != nullptr)\n    {\n";
            // 文本长度必须夹住：旧的 `memcpy(strlen + 1)` 在文本超过 255 字节时写穿整个槽
            cpp += "        const std::size_t room = sizeof(slot.text) - 1u;\n";
            cpp += "        const std::size_t length = (::strlen(text) < room) ? ::strlen(text) : room;\n";
            cpp += "        ::memcpy(slot.text, text, length);\n        slot.text[length] = 0;\n    }\n}\n";
            cpp += "static int32_t " + shape.name + "_readErrorCode(void* self)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + shape.name + "_errorMutex);\n";
            cpp += "    return " + shape.name + "_errorSlotLocked(self).code;\n}\n";
            cpp += "static void " + shape.name + "_readErrorText(void* self, char* dst, uint64_t cap)\n{\n";
            cpp += "    if (cap == 0u || dst == nullptr) { return; }\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + shape.name + "_errorMutex);\n";
            cpp += "    const " + shape.name + "ErrorSlot& slot = " + shape.name + "_errorSlotLocked(self);\n";
            cpp += "    const std::size_t length = ::strlen(slot.text);\n";
            cpp += "    const std::size_t copied = (length < cap - 1u) ? length : cap - 1u;\n";
            cpp += "    ::memcpy(dst, slot.text, copied);\n    dst[copied] = 0;\n}\n";
            // 旧的独立入口保留（既有 CoreTests 与 C ABI 用户还在调），但**必须转发到同一份状态**：
            // 否则"从这里读不到错误、只有表里读得到"会把同一次失败读成两个数。
            cpp += "extern \"C\" int32_t " + shape.name + "_LastErrorCode(void* self)\n{\n    return " +
                   shape.name + "_readErrorCode(self);\n}\n";
            cpp += "extern \"C\" int32_t " + shape.name + "_LastErrorText(void* self, char* dst, uint64_t cap)\n{\n";
            cpp += "    " + shape.name + "_readErrorText(self, dst, cap);\n";
            cpp += "    return (dst != nullptr && cap > 0u) ? (int32_t)::strlen(dst) : 0;\n}\n\n";
            // ---- 句柄表的实现（所有接口共用一张表，任务 5.11 / design D12 乙）----
            cpp += "static cascaded_bindings::HandleEntry* " + shape.name + "_claimSlotLocked()\n{\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    uint64_t index = 1u;\n";
            cpp += "    while (index <= indexMask && index < " + shape.name + "_handles.size() + 1u)\n    {\n";
            cpp += "        if (" + shape.name + "_handles[index - 1u].kind == 0u) { break; }\n        ++index;\n    }\n";
            cpp += "    if (index > indexMask) { return nullptr; }\n";
            cpp += "    if (index > " + shape.name + "_handles.size()) { " + shape.name + "_handles.push_back({}); }\n";
            cpp += "    return &" + shape.name + "_handles[index - 1u];\n}\n";
            cpp += "static uint64_t " + shape.name +
                   "_handleOfLocked(cascaded_bindings::HandleEntry const* entry)\n{\n";
            cpp += "    const uint64_t index = static_cast<uint64_t>(entry - " + shape.name +
                   "_handles.data()) + 1u;\n";
            cpp += "    return (static_cast<uint64_t>(entry->generation) << " + shape.name +
                   "_HANDLE_INDEX_BITS) | index;\n}\n";
            cpp += "static uint64_t " + shape.name + "_registerStrongHandle(void const* owner, std::shared_ptr<void> object, char const* targetType)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    if (!object) { " + shape.name + "_setError(owner, " + shape.name + "_ERROR_NULL_POINTER, \"实现返回了空指针\"); return 0u; }\n";
            cpp += "    cascaded_bindings::HandleEntry* entry = " + shape.name + "_claimSlotLocked();\n";
            cpp += "    if (entry == nullptr) { return 0u; }\n";
            cpp += "    entry->object = object.get();\n    entry->kind = 1u;\n    entry->hasWitness = false;\n";
            cpp += "    entry->witnessHandle = 0u;\n";
            cpp += "    entry->interfaceTag = &" + shape.name + "_HANDLE_TAG;\n";
            cpp += "    entry->issuerType = \"" + shape.qualifiedName + "\";\n";
            cpp += "    entry->targetType = targetType;\n";
            cpp += "    entry->strong = object;\n    entry->weak.reset();\n";
            cpp += "    return " + shape.name + "_handleOfLocked(entry);\n}\n";
            cpp += "static uint64_t " + shape.name + "_registerWeakHandle(void const* owner, std::weak_ptr<void> object, char const* targetType)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    if (object.expired()) { " + shape.name + "_setError(owner, " + shape.name + "_ERROR_NULL_POINTER, \"实现返回了空指针\"); return 0u; }\n";
            cpp += "    cascaded_bindings::HandleEntry* entry = " + shape.name + "_claimSlotLocked();\n";
            cpp += "    if (entry == nullptr) { return 0u; }\n";
            // 弱句柄不持有强引用，所以 object 先写 weak 换出来的指针；对象一死 lookup 就失败
            cpp += "    entry->object = object.lock().get();\n    entry->kind = 2u;\n    entry->hasWitness = false;\n";
            cpp += "    entry->witnessHandle = 0u;\n";
            cpp += "    entry->interfaceTag = &" + shape.name + "_HANDLE_TAG;\n";
            cpp += "    entry->issuerType = \"" + shape.qualifiedName + "\";\n";
            cpp += "    entry->targetType = targetType;\n";
            cpp += "    entry->weak = object;\n    entry->strong.reset();\n";
            cpp += "    return " + shape.name + "_handleOfLocked(entry);\n}\n";
            // 借用句柄（任务 5.3 / design D9）：只存裸指针、不持有；发行者见证 = 发行者在表里那个
            // **活着的表项**（按句柄与代际判，而不是按对象弱引用 —— 别的持有者会让弱引用一直不过期，
            // 于是"见证过期"永远判不出来）。
            cpp += "static bool " + shape.name + "_witnessAliveLocked(uint64_t witnessHandle)\n{\n";
            cpp += "    if (witnessHandle == 0u) { return false; }\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = witnessHandle & indexMask;\n";
            cpp += "    const uint64_t generation = witnessHandle >> " + shape.name + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + shape.name + "_handles.size()) { return false; }\n";
            cpp += "    auto const& entry = " + shape.name + "_handles[index - 1u];\n";
            cpp += "    return entry.kind != 0u && entry.kind != 3u && entry.generation == generation;\n}\n";
            cpp += "static uint64_t " + shape.name + "_registerBorrowedHandle(void const* issuer, void* object, char const* targetType)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    if (object == nullptr) { " + shape.name + "_setError(issuer, " + shape.name + "_ERROR_NULL_POINTER, \"实现返回了空指针\"); return 0u; }\n";
            cpp += "    cascaded_bindings::HandleEntry* entry = " + shape.name + "_claimSlotLocked();\n";
            cpp += "    if (entry == nullptr) { return 0u; }\n";
            cpp += "    entry->object = object;\n    entry->kind = 4u;\n    entry->hasWitness = false;\n";
            cpp += "    entry->witnessHandle = 0u;\n";
            cpp += "    entry->interfaceTag = &" + shape.name + "_HANDLE_TAG;\n";
            cpp += "    entry->issuerType = \"" + shape.qualifiedName + "\";\n";
            cpp += "    entry->targetType = targetType;\n";
            cpp += "    entry->strong.reset();\n    entry->weak.reset();\n";
            cpp += "    for (std::size_t slot = 0; slot < " + shape.name + "_handles.size(); ++slot)\n    {\n";
            cpp += "        auto const& other = " + shape.name + "_handles[slot];\n";
            cpp += "        if (other.object != issuer || other.kind == 0u || other.kind == 3u) { continue; }\n";
            cpp += "        entry->hasWitness = true;\n";
            cpp += "        entry->witnessHandle = (static_cast<uint64_t>(other.generation) << " + shape.name +
                   "_HANDLE_INDEX_BITS) | (static_cast<uint64_t>(slot) + 1u);\n";
            cpp += "        break;\n    }\n";
            cpp += "    return " + shape.name + "_handleOfLocked(entry);\n}\n";
            cpp += "static std::shared_ptr<void> " + shape.name +
                   "_lookupStrongHandle(void const* owner, uint64_t handle, char const* expectedTarget, char const* roleText)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            cpp += "    const uint64_t generation = handle >> " + shape.name + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + shape.name + "_handles.size())\n    {\n";
            cpp += "        " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, roleText);\n        return {};\n    }\n";
            cpp += "    auto& entry = " + shape.name + "_handles[index - 1u];\n";
            cpp += "    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation)\n    {\n";
            cpp += "        " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, roleText);\n        return {};\n    }\n";
            cpp += "    if (expectedTarget != nullptr && expectedTarget[0] != 0 &&\n";
            cpp += "        (entry.targetType == nullptr || ::strcmp(entry.targetType, expectedTarget) != 0))\n    {\n";
            cpp += "        " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"句柄的目标类型不符\");\n        return {};\n    }\n";
            cpp += "    if (entry.kind == 4u)\n    {\n";
            cpp += "        " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"借用句柄没有可持有的智能指针所有权\");\n        return {};\n    }\n";
            cpp += "    if (entry.kind == 2u)\n    {\n";
            cpp += "        std::shared_ptr<void> locked = entry.weak.lock();\n";
            cpp += "        if (!locked) { " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, roleText); }\n";
            cpp += "        return locked;\n    }\n";
            cpp += "    return entry.strong;\n}\n";
            cpp += "static void* " + shape.name + "_lookupHandle(uint64_t handle)\n{\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            cpp += "    const uint64_t generation = handle >> " + shape.name + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + shape.name + "_handles.size()) { return nullptr; }\n";
            cpp += "    auto const& entry = " + shape.name + "_handles[index - 1u];\n";
            cpp += "    /* kind 3 = 已退休的槽：代际回绕过，永不再分配 */\n    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation) { return nullptr; }\n";
            // 标签校验（任务 5.11）：数字相同、但属于别的接口的句柄，**不许**在这里命中
            cpp += "    if (entry.interfaceTag != &" + shape.name + "_HANDLE_TAG) { return nullptr; }\n";
            // 弱句柄：每次取用都 lock 一次，对象已析构就判失效。
            // 这里**不写错误码**：错误槽按 self 分，而查表函数不知道调用方是谁 ——
            // 由两个入口（`ResolveHandle` / `releaseHandle`）各自按自己的 self 置码。
            cpp += "    if (entry.kind == 2u)\n    {\n";
            cpp += "        auto locked = entry.weak.lock();\n";
            cpp += "        if (!locked) { return nullptr; }\n";
            cpp += "        return locked.get();\n    }\n";
            // 借用句柄（旧发行者路径）：有见证就按见证判；没有见证仍允许显式释放（兼容行为）。
            cpp += "    if (entry.kind == 4u)\n    {\n";
            cpp += "        if (entry.hasWitness && !" + shape.name +
                   "_witnessAliveLocked(entry.witnessHandle)) { return nullptr; }\n";
            cpp += "        return const_cast<void*>(entry.object);\n    }\n";
            cpp += "    return const_cast<void*>(entry.object);\n}\n";
            cpp += "static void* " + shape.name +
                   "_lookupTargetHandle(void const* owner, uint64_t handle, char const* expectedTarget)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            cpp += "    const uint64_t generation = handle >> " + shape.name + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + shape.name + "_handles.size())\n    {\n";
            cpp += "        " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"句柄已失效\");\n        return nullptr;\n    }\n";
            cpp += "    auto& entry = " + shape.name + "_handles[index - 1u];\n";
            cpp += "    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation)\n    {\n";
            cpp += "        " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"句柄已失效\");\n        return nullptr;\n    }\n";
            cpp += "    if (entry.targetType == nullptr || ::strcmp(entry.targetType, expectedTarget) != 0)\n    {\n";
            cpp += "        " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"句柄的目标类型不符\");\n        return nullptr;\n    }\n";
            cpp += "    if (entry.kind == 2u)\n    {\n";
            cpp += "        std::shared_ptr<void> locked = entry.weak.lock();\n";
            cpp += "        if (!locked) { " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"目标对象已死亡\"); return nullptr; }\n";
            cpp += "        return locked.get();\n    }\n";
            cpp += "    if (entry.kind == 4u)\n    {\n";
            cpp += "        if (!entry.hasWitness || !" + shape.name + "_witnessAliveLocked(entry.witnessHandle))\n        {\n";
            cpp += "            " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"借用句柄的发行者见证不存在或已过期\");\n            return nullptr;\n        }\n";
            cpp += "        return const_cast<void*>(entry.object);\n    }\n";
            cpp += "    return const_cast<void*>(entry.object);\n}\n";
            const std::string releaseEntryCode =
                "    entry.kind = 0u;\n    entry.object = nullptr;\n"
                "    entry.strong.reset();\n    entry.weak.reset();\n"
                "    entry.hasWitness = false;\n    entry.witnessHandle = 0u;\n"
                "    entry.issuerType = nullptr;\n    entry.targetType = nullptr;\n"
                "    const uint32_t nextGeneration = (entry.generation + 1u) & ((1u << " + shape.name +
                "_HANDLE_GEN_BITS) - 1u);\n"
                "    if (nextGeneration == 0u) { entry.kind = 3u; }\n"
                "    entry.generation = nextGeneration;\n";
            // 释放 = 表项清空 + 强/弱引用一起放掉 + 代际进位（旧句柄因此被判 stale）
            cpp += "static void " + shape.name + "_releaseHandle(void const* owner, uint64_t handle)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            // 释放走一次 lookup：标签不符 / 已失效都在这里被挡下来，错误码归调用方那个实例
            cpp += "    if (" + shape.name + "_lookupHandle(handle) == nullptr)\n    {\n        " + shape.name +
                   "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"stale handle\");\n        return;\n    }\n";
            cpp += "    auto& entry = " + shape.name + "_handles[index - 1u];\n";
            cpp += releaseEntryCode + "}\n\n";
            // 目标释放：先按序号、代际和目标类型校验，再清表项并进位。目标已死亡或见证过期**不**
            // 阻止清理 —— 这条路径必须能把残槽收掉。
            cpp += "static void " + shape.name +
                   "_releaseTargetHandle(void const* owner, uint64_t handle, char const* expectedTarget)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + handleMutexName + ");\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            cpp += "    const uint64_t generation = handle >> " + shape.name + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + shape.name + "_handles.size())\n    {\n";
            cpp += "        " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"句柄已失效\");\n        return;\n    }\n";
            cpp += "    auto& entry = " + shape.name + "_handles[index - 1u];\n";
            cpp += "    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation)\n    {\n";
            cpp += "        " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"句柄已失效\");\n        return;\n    }\n";
            cpp += "    if (entry.targetType == nullptr || ::strcmp(entry.targetType, expectedTarget) != 0)\n    {\n";
            cpp += "        " + shape.name + "_setError(owner, " + shape.name + "_ERROR_STALE_HANDLE, \"句柄的目标类型不符\");\n        return;\n    }\n";
            cpp += releaseEntryCode + "}\n\n";
            // 每接口两个泛用入口（任务 1.3）：`ResolveHandle` 是必需的 —— C ABI 面所有方法入口
            // 的第一个参数都是 `void* self`，没有任何一个收定宽句柄；没有它，C# 拿到句柄之后
            // 没有任何办法用它调方法，"旧句柄报 1001" 这条验收也写不出来。
            // 任务 5.10：`ResolveHandle` 也收 `self` —— 它是最常失败的那个入口，不给自己就没法
            // 把 1001 记到"哪个实例失败了"上（错误槽按 self 分）。
            cpp += "extern \"C\" void " + shape.name + "_ReleaseHandle(void* self, uint64_t handle)\n{\n";
            cpp += "    " + shape.name + "_clearError(self);\n";
            cpp += "    " + shape.name + "_releaseHandle(self, handle);\n}\n\n";
            cpp += "extern \"C\" void* " + shape.name + "_ResolveHandle(void* self, uint64_t handle)\n{\n";
            cpp += "    " + shape.name + "_clearError(self);\n";
            // 任务 5.1（`csharp-visible-window`）：解析这条路原来**裸读**句柄表 —— 登记时会 push_back
            // 扩容、释放时清项，两边都持 HandleMutex，只有这里不持，于是并发用例偶发"取不到句柄"
            // （实测 353 次里 9 次红）。三路共用同一把锁，读表也在锁内。
            cpp += "    std::lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex());\n";
            cpp += "    void* object = " + shape.name + "_lookupHandle(handle);\n";
            cpp += "    if (object == nullptr) { " + shape.name + "_setError(self, " + shape.name +
                   "_ERROR_STALE_HANDLE, \"stale handle\"); }\n";
            cpp += "    return object;\n}\n\n";
            // 目标接口入口（任务 3.2）：按目标对象的完整限定类型解析/释放；只追加，不重排旧槽。
            cpp += "extern \"C\" void* " + shape.name + "_ResolveTargetHandle(void* self, uint64_t handle)\n{\n";
            cpp += "    " + shape.name + "_clearError(self);\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex());\n";
            cpp += "    return " + shape.name + "_lookupTargetHandle(self, handle, \"" + shape.qualifiedName +
                   "\");\n}\n\n";
            cpp += "extern \"C\" void " + shape.name + "_ReleaseTargetHandle(void* self, uint64_t handle)\n{\n";
            cpp += "    " + shape.name + "_clearError(self);\n";
            cpp += "    " + shape.name + "_releaseTargetHandle(self, handle, \"" + shape.qualifiedName +
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
                    // 返回类型：两段式字符串 → string；弱/借用句柄 → 同名接口；其余按表面类型映射
                    std::string surfaceReturn;
                    if (stringTwoStep)
                    {
                        surfaceReturn = "string";
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
                    };
                    std::vector<SurfaceParam> surfaceParams;
                    std::vector<std::string> tupleParts;
                    for (std::size_t paramIndex = 0; paramIndex < method.params.size(); ++paramIndex)
                    {
                        auto const& parameter = method.params[paramIndex];
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
                            auto known = g_knownInterfaces.find(innerShort);
                            if (known == g_knownInterfaces.end())
                            {
                                supported = false;
                                break;
                            }
                            SurfaceParam surfaceParam;
                            surfaceParam.type = innerShort;
                            surfaceParam.name = parameter.second;
                            surfaceParam.isSurfaceObject = true;
                            surfaceParam.targetType = known->second;
                            surfaceParams.push_back(surfaceParam);
                            continue;
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
                    // 返回值：出参在场就换成元组（只有一个出参时不套元组）
                    std::string callReturn = surfaceReturn;
                    if (tupleParts.size() == 1)
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
                        callBody += "            ulong length = " + bindingsName + "." + method.name +
                                    "Len(lease.Self);\n";
                        callBody += throwLine;
                        callBody += "            if (length == 0UL) { return string.Empty; }\n";
                        callBody += "            byte[] buffer = new byte[(int)length + 1];\n";
                        callBody += "            fixed (byte* bufferPtr = buffer) { " + bindingsName + ".Copy" +
                                    method.name + "(lease.Self, bufferPtr, (ulong)buffer.Length); }\n";
                        callBody += throwLine;
                        callBody += "            return System.Text.Encoding.UTF8.GetString(buffer, 0, (int)length);\n";
                    }
                    else if (!surfaceObjectName.empty())
                    {
                        // 强/弱/借用返回：C ABI 给的是句柄，这里用**目标接口**的解析/释放入口换成表面对象。
                        // 目标类型不符、已失效、借用无见证都会让解析返回 null 并把具体文本写进错误通道。
                        callBody += "            ulong handle = " + bindingsName + "." + method.name + "(" + abiArgs +
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
                        callBody += "            " + bindingsName + "." + method.name + "(" + abiArgs + ");\n";
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
                    else if (surface.returnType == "void")
                    {
                        callBody += "            " + bindingsName + "." + method.name + "(" + abiArgs + ");\n";
                        callBody += throwLine;
                    }
                    else if (surfaceMirrorReturn != nullptr)
                    {
                        // 镜像记录按值返回：C ABI 给 `<接口>Bindings.<T>C`，FromC 还原成公开记录
                        callBody += "            " + shape.name + "Bindings." + surfaceMirrorReturn->cName +
                                    " result = " + bindingsName + "." + method.name + "(" + abiArgs + ");\n";
                        callBody += throwLine;
                        callBody += "            return FromC" + surfaceMirrorReturn->csName + "(result);\n";
                    }
                    else if (method.returnKind == static_cast<int>(CXType_Enum))
                    {
                        // 普通枚举返回：ABI 是 int32_t，公开表面还原成枚举类型
                        EnumInfo const* info = findEnumBySpelling(method.returnCanonicalSpelling);
                        callBody += "            int result = " + bindingsName + "." + method.name + "(" + abiArgs +
                                    ");\n";
                        callBody += throwLine;
                        callBody += "            return (" + (info != nullptr ? info->shortName : std::string("int")) +
                                    ")result;\n";
                    }
                    else if (surface.returnType == "uint64_t")
                    {
                        // `void*` → 不透明令牌 → IntPtr
                        callBody += "            ulong value = " + bindingsName + "." + method.name + "(" + abiArgs +
                                    ");\n";
                        callBody += throwLine;
                        callBody += "            return (IntPtr)(void*)value;\n";
                    }
                    else
                    {
                        callBody += "            " + toCSharpType(surface.returnType) + " result = " + bindingsName + "." +
                                    method.name + "(" + abiArgs + ");\n";
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
                    }
                    cs += callBody;
                    cs += "        }\n\n";

                    // ---- 表面方法：一行转发 ----
                    surfaceClass += "        public " + callReturn + " " + surfaceSignature + "\n        {\n";
                    surfaceClass += (callReturn == "void" ? "            " : "            return ") + callsName + "." +
                                    method.name + "(m_lease";
                    for (SurfaceParam const& param : surfaceParams)
                    {
                        if (!param.isOut)
                        {
                            surfaceClass += ", " + param.name;
                        }
                    }
                    surfaceClass += ");\n        }\n";
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
            const std::string cppPath = std::string(outDir) + "/" + shape.name + ".Bindings.cpp";
            const std::string csPath = std::string(outDir) + "/" + shape.name + ".Bindings.cs";
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
            for (auto const& entry : publicEnums)
            {
                const std::string path = std::string(outDir) + "/" + entry.first + ".Bindings.cs";
                std::ofstream out(path, std::ios::binary);
                out << emitPublicEnumFile(entry.second);
                if (!out)
                {
                    std::fprintf(stderr, "CodeGen: cannot write '%s'\n", path.c_str());
                    return 2;
                }
                std::fprintf(stderr, "[共享类型] %s=%s\n", entry.first.c_str(),
                             entry.second.cppName.c_str());
            }
            for (auto const& entry : publicRecords)
            {
                const std::string path = std::string(outDir) + "/" + entry.second.csName + ".Bindings.cs";
                std::ofstream out(path, std::ios::binary);
                out << emitPublicRecordFile(entry.second);
                if (!out)
                {
                    std::fprintf(stderr, "CodeGen: cannot write '%s'\n", path.c_str());
                    return 2;
                }
                std::fprintf(stderr, "[共享类型] %s=%s\n", entry.second.csName.c_str(),
                             entry.second.cppName.c_str());
            }
        }

        std::fprintf(stderr, "绑定：接口 %zu 个 / 方法 %d 条 / 产物 %d 个 / 拒绝 %d 条\n",
                     shapes.size(), methodsTotal, products, rejected);
        if (rejected > 0)
        {
            return 1;
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

        std::map<std::string, std::string> listed;
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
            auto existing = listed.find(found->name);
            if (existing != listed.end() && existing->second != found->qualifiedName)
            {
                std::fprintf(stderr,
                             "CodeGen: 拒绝 —— 同短名接口 %s 有两种完整限定名：%s / %s\n",
                             found->name.c_str(), existing->second.c_str(), found->qualifiedName.c_str());
                return 1;
            }
            listed[found->name] = found->qualifiedName;
        }
        for (auto const& entry : listed)
        {
            std::printf("%s=%s\n", entry.first.c_str(), entry.second.c_str());
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr,
            "usage: CodeGen <file.h> <TypeName> [--emit-reflection] [--qualified-name=<qn>] [-o <out>] [clang-arg ...]\n"
            "       CodeGen <file.h> <TypeName> --emit-all --out-dir <dir> [--qualified-name=<qn>] [clang-arg ...]\n"
            "       --emit-all writes ONE file per type: <dir>/<TypeName>.codegen.h (reflection + serialization + hash).\n");
        return 2;
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
    std::map<std::string, std::string> knownInterfaceArgs;
    std::vector<std::string> knownInterfaceConflicts;
    auto addKnownInterface = [&knownInterfaceArgs, &knownInterfaceConflicts](std::string const& text) {
        const std::size_t split = text.find('=');
        if (split == std::string::npos || split == 0 || split + 1 >= text.size())
        {
            std::fprintf(stderr, "CodeGen: --known-interface 需要 <短名>=<完整限定名>（收到 '%s'）\n",
                         text.c_str());
            return;
        }
        const std::string shortName = text.substr(0, split);
        const std::string qualified = text.substr(split + 1);
        auto existing = knownInterfaceArgs.find(shortName);
        if (existing != knownInterfaceArgs.end() && existing->second != qualified)
        {
            knownInterfaceConflicts.push_back(shortName + "：" + existing->second + " / " + qualified);
            return;
        }
        knownInterfaceArgs[shortName] = qualified;
    };
    const char* outPath           = nullptr;
    const char* outDir            = nullptr;
    std::string qualifiedNameOverride;
    std::string auditScript;   // --audit-script：审计脚本路径（默认取头文件同目录）
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

    // 同短名不同完整限定名：清单本身就必须拒绝，不能靠后一次覆盖前一次（任务 3.1）。
    if (!knownInterfaceConflicts.empty())
    {
        for (std::string const& conflict : knownInterfaceConflicts)
        {
            std::fprintf(stderr, "CodeGen: 拒绝 —— 同短名接口有两种完整限定名：%s\n", conflict.c_str());
        }
        return 1;
    }
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
        const int bindingResult =
            bindemit::run(filePath, outDir, clangArgs, auditScript, handleIndexBits, handleGenerationBits);
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