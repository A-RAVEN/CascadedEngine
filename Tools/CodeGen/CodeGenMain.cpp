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

    struct MethodSignature
    {
        std::string name;
        std::string returnSpelling;
        int returnKind = 0;                                        // CXTypeKind 的取值，用来认枚举
        std::vector<std::pair<std::string, std::string>> params;   // （类型拼写，形参名）
        std::vector<int> paramKinds;
        bool isConst = false;
    };

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
        method.returnSpelling = toStd(clang_getTypeSpelling(clang_getResultType(functionType)));
        method.returnKind = static_cast<int>(clang_getCanonicalType(clang_getResultType(functionType)).kind);

        const int count = clang_Cursor_getNumArguments(cursor);
        for (int index = 0; index < count; ++index)
        {
            const CXCursor argument = clang_Cursor_getArgument(cursor, static_cast<unsigned>(index));
            const CXType argumentCXType = clang_getCursorType(argument);
            const std::string argumentType = toStd(clang_getTypeSpelling(argumentCXType));
            const std::string argumentName = toStd(clang_getCursorSpelling(argument));
            method.params.emplace_back(argumentType, argumentName);
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

    struct Projected
    {
        std::string type;
        std::string name;
    };

    struct MirrorField
    {
        std::string name;
        std::string cType;
        uint64_t offsetBytes = 0;
    };

    struct MirrorType
    {
        std::string cppName;   // C++ 侧结构体名（例如 BindScalarThing）
        std::string cName;     // C 侧镜像名（例如 BindScalarThingC）
        uint64_t sizeBytes = 0;
        std::vector<MirrorField> fields;
        bool ok = true;
        std::string whyNot;
    };

    struct MirrorCollector
    {
        std::set<std::string> const* wanted = nullptr;
        std::vector<MirrorType>* sink = nullptr;
    };

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

    CXChildVisitResult collectMirrorTypes(CXCursor cursor, CXCursor, CXClientData data)
    {
        auto* collector = static_cast<MirrorCollector*>(data);
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind != CXCursor_StructDecl && kind != CXCursor_ClassDecl)
        {
            return CXChildVisit_Recurse;
        }
        // 只收**定义**：前向声明那一次访问拿不到形参，收到就会生成空签名（实测把 16 条方法弄成 32 条、
        // 对拍多出 18 处不符）
        if (clang_isCursorDefinition(cursor) == 0)
        {
            return CXChildVisit_Recurse;
        }
        const std::string name = toStd(clang_getCursorSpelling(cursor));
        if (collector->wanted->find(name) == collector->wanted->end())
        {
            return CXChildVisit_Recurse;
        }

        MirrorType mirror;
        mirror.cppName = qualifiedCursorName(cursor);
        mirror.cName = name + "C";
        const long long size = clang_Type_getSizeOf(clang_getCursorType(cursor));
        if (size <= 0)
        {
            mirror.ok = false;
            mirror.whyNot = "取不到大小（不完整的类型？）";
            collector->sink->push_back(std::move(mirror));
            return CXChildVisit_Recurse;
        }
        mirror.sizeBytes = static_cast<uint64_t>(size);

        struct FieldWalk
        {
            MirrorType* mirror;
        } walk{ &mirror };
        clang_visitChildren(
            cursor,
            [](CXCursor fieldCursor, CXCursor, CXClientData fieldData) {
                auto* state = static_cast<FieldWalk*>(fieldData);
                if (clang_getCursorKind(fieldCursor) == CXCursor_CXXMethod)
                {
                    // 有方法的类型不是"纯数据"，不可能是 C 侧镜像（实测：IWindowSystem 被当成镜像，
                    // 生成的 static_assert 当场炸）
                    state->mirror->ok = false;
                    state->mirror->whyNot = "类型带方法（不是纯数据结构）";
                    return CXChildVisit_Continue;
                }
                if (clang_getCursorKind(fieldCursor) != CXCursor_FieldDecl)
                {
                    return CXChildVisit_Continue;
                }
                MirrorField field;
                field.name = toStd(clang_getCursorSpelling(fieldCursor));
                field.cType = toStd(clang_getTypeSpelling(clang_getCursorType(fieldCursor)));
                const long long bitOffset = clang_Cursor_getOffsetOfField(fieldCursor);
                field.offsetBytes = bitOffset > 0 ? static_cast<uint64_t>(bitOffset) / 8u : 0u;
                state->mirror->fields.push_back(std::move(field));
                return CXChildVisit_Continue;
            },
            &walk);

        // 只覆盖"成员全是标量"的结构体；别的情况**明说跳过**，不猜布局。
        for (MirrorField const& field : mirror.fields)
        {
            if (!isScalarType(bareType(field.cType)))
            {
                mirror.ok = false;
                mirror.whyNot = "成员 " + field.name + " 的类型是 " + field.cType + "（非标量，布局清单暂不覆盖）";
                break;
            }
        }
        collector->sink->push_back(std::move(mirror));
        return CXChildVisit_Recurse;
    }

    // 生成布局清单：C++ 侧结构体的镜像 + `static_assert(sizeof/offsetof)`，外加 C# 侧的同源常量。
    // 静态断言是关键：只有"两份同源常量互相比较"时，生成器算错布局会让两侧一起错、自检照样绿。
    void emitLayoutManifest(std::string* cpp, std::string* cs, std::vector<MirrorType> const& mirrors)
    {
        for (MirrorType const& mirror : mirrors)
        {
            if (!mirror.ok)
            {
                *cpp += "// 布局清单跳过 " + mirror.cppName + "：" + mirror.whyNot + "\n";
                continue;
            }
            *cpp += "typedef struct " + mirror.cName + " {\n";
            for (MirrorField const& field : mirror.fields)
            {
                *cpp += "    " + field.cType + " " + field.name + ";\n";
            }
            *cpp += "} " + mirror.cName + ";\n";
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
            *cpp += "\n";
            *cs += "        // 布局清单（与 C++ 侧同源常量；不一致由 C++ 的 static_assert 先兜住）\n";
            *cs += "        public const int " + mirror.cName + "_Size = " +
                   std::to_string(mirror.sizeBytes) + ";\n";
            for (MirrorField const& field : mirror.fields)
            {
                *cs += "        public const int " + mirror.cName + "_" + field.name + "_Offset = " +
                       std::to_string(field.offsetBytes) + ";\n";
            }
        }
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
        if (cType == "void")
        {
            return "void";
        }
        if (cType == "int32_t" || cType == "int")
        {
            return "int";
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

    // 把一条方法投影成 C ABI 的（参数们, 返回类型）。返回 false 表示该形状必须被拒绝。
    bool projectMethod(MethodSignature const& method, std::vector<Projected>* params, std::string* returnType,
                       std::string* rejectReason, bool* needsTwoStep, bool* isHandleReturn)
    {
        *needsTwoStep = false;
        *isHandleReturn = false;
        params->clear();
        *returnType = "void";

        for (auto const& param : method.params)
        {
            const std::string spelling = trimCopy(param.first);
            const std::string bare = bareType(spelling);
            const std::string name = param.second.empty() ? std::string("arg") : param.second;

            if (hasPrefix(bare, "castl::weak_ptr") || hasPrefix(bare, "std::weak_ptr"))
            {
                *rejectReason = "② 所有权不明裸指针：weak_ptr 的弱引用语义在 C# 侧没有对应物（不投影）";
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
                // 回调参数（任务 2.2）：投影成函数指针 + `void* user` 还不够 —— 还要保活表 +
                // 注销句柄，本轮没做，所以**拒绝**而不是硬发一个固定签名（旧行为一律投成
                // `void (*)(void*, uint32_t, float)`，与真实回调签名无关，编都编不过）。
                *rejectReason = "本轮未实现：缺回调投影路径（保活表 + 注销句柄）";
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
                 method.returnSpelling.find('*') != std::string::npos)
        {
            // 所有权不明的裸指针返回（`T*`）：审计表对这类要求走句柄表 + 配对释放，
            // 一律投成裸令牌等于把所有权不明的指针静默绑成裸地址，所以必须由作者显式排除。
            *rejectReason = "② 所有权不明裸指针：返回裸指针且没有所有权说明（必须显式排除或改成句柄）";
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
        // 布局清单要覆盖哪些结构体：把每条方法先投影一遍（纯函数），收投影后类型里以 `C` 结尾的名字。
        std::set<std::string> wantedMirrors;
        {
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
            for (InterfaceShape const& shape : shapes)
            {
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
            }
        }

        std::vector<MirrorType> mirrors;
        if (!wantedMirrors.empty())
        {
            MirrorCollector mirrorCollector;
            mirrorCollector.wanted = &wantedMirrors;
            mirrorCollector.sink = &mirrors;
            clang_visitChildren(clang_getTranslationUnitCursor(unit), collectMirrorTypes, &mirrorCollector);
            // 同一个结构体会被访问多次（前向声明 + 定义），按名字去重
            std::vector<MirrorType> unique;
            std::set<std::string> seen;
            for (MirrorType& mirror : mirrors)
            {
                if (seen.insert(mirror.cppName).second)
                {
                    unique.push_back(std::move(mirror));
                }
            }
            mirrors = std::move(unique);
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
        for (InterfaceShape const& shape : shapes)
        {
            std::string cpp;
            cpp += "// 由 CodeGen --emit-binding 生成，勿手改。\n#pragma once\n#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n#include <mutex>\n";
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
            cpp += "\n";
            std::string cs;
            cs += "// 由 CodeGen --emit-binding 生成，勿手改。\n";
            cs += "using System.Runtime.InteropServices;\n\n";
            cs += "namespace Scripting.Generated.Bindings\n{\n";
            cs += "    /// <summary>" + shape.name + " 的句柄：C 侧是不透明 uint64，0 表示无效</summary>\n";
            cs += "    [StructLayout(LayoutKind.Sequential)]\n";
            cs += "    public readonly struct " + shape.name + "Handle\n    {\n";
            cs += "        public readonly ulong Raw;\n";
            cs += "        public " + shape.name + "Handle(ulong raw) { Raw = raw; }\n";
            cs += "        public bool IsValid => Raw != 0UL;\n";
            cs += "    }\n\n";
            cs += "    internal static unsafe class " + shape.name + "Bindings\n    {\n";

            cs += "        public static delegate* unmanaged<void*, int> LastErrorCode;\n";
            cs += "        public static delegate* unmanaged<void*, byte*, ulong, int> LastErrorText;\n";

            int emitted = 0;
            // 每个接口一对错误通道：托管侧靠它把"异常 → 错误码 + 文本"读回来（异常绝不穿过 C ABI）。
            cpp += "extern \"C\" int32_t " + shape.name + "_LastErrorCode(void* self);\n";
            cpp += "extern \"C\" int32_t " + shape.name + "_LastErrorText(void* self, char* dst, uint64_t cap);\n";
            // 布局清单（任务 2.5）：镜像结构体 + 编译期断言。必须**在方法声明的循环之前**发出 ——
            // 声明里会用到镜像类型，放在后面就是"先用后定义"（实测 C4430 / C2378）。
            emitLayoutManifest(&cpp, &cs, mirrors);
            std::string tableFields;   // 函数表里的成员声明
            std::string tableInit;     // 函数表的初始化列表
            std::string thunkDefs;     // thunk 实现
            std::string externDefs;    // 导出入口（extern "C" 的定义）
            std::string lateDefs;      // 必须排在句柄表实现之后的定义（Release 之类）
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
                    std::fprintf(stderr, "CodeGen: 拒绝 %s::%s —— %s\n", shape.name.c_str(),
                                 method.name.c_str(), rejectReason.c_str());
                    ++rejected;
                    continue;
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
                    lateDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    lateDefs += "    return static_cast<uint64_t>(object->" + method.name + "().size());\n}\n";
                    lateDefs += "extern \"C\" void " + shape.name + "_Copy" + method.name +
                                "(void* self, char* dst, uint64_t cap)\n{\n";
                    // 容量是无符号的：`cap - 1` 在 0 时会下溢成最大值，必须先判
                    lateDefs += "    if (cap == 0u || dst == nullptr) { return; }\n";
                    lateDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    lateDefs += "    const auto value = object->" + method.name + "();\n";
                    lateDefs += "    const std::size_t length = static_cast<std::size_t>(value.size());\n";
                    lateDefs += "    const std::size_t copied = (length < cap - 1u) ? length : cap - 1u;\n";
                    lateDefs += "    ::memcpy(dst, value.data(), copied);\n";
                    lateDefs += "    dst[copied] = 0;\n}\n";
                    continue;
                }
                std::string signature = returnType + " " + shape.name + "_" + method.name + "(void* self";
                for (Projected const& projected : params)
                {
                    signature += ", " + projected.type + " " + projected.name;
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
                    lateDefs += "extern \"C\" void " + shape.name + "_Release" + method.name +
                                "(void*, uint64_t handle)\n{\n    " + shape.name +
                                "_releaseHandle(handle);\n}\n";
                }

                // C# 侧的真实签名：`delegate* unmanaged<参数…, 返回>`
                {
                    std::string delegate = "        public static delegate* unmanaged<";
                    delegate += "void*";
                    for (Projected const& projected : params)
                    {
                        delegate += ", " + toCSharpType(projected.type);
                    }
                    delegate += ", " + toCSharpType(returnType) + "> " + method.name + ";\n";
                    cs += delegate;
                }
                ++emitted;

                // 函数表成员：句柄作首参（`void* self`），其余按投影后的类型串起来（不含形参名）
                std::string paramTypes = "void*";
                for (Projected const& projected : params)
                {
                    paramTypes += ", " + projected.type;
                }
                tableFields += "    " + returnType + " (*" + method.name + ")(" + paramTypes + ");\n";
                tableInit += "    &" + shape.name + "_thunk_" + method.name + ",\n";

                // thunk 实现：先只覆盖"参数全是标量、返回是 void 或同名标量"的形状；
                // 其它形状（容器 / 两段式 / 句柄 / 回调 / 出参）留给 2.4 / 2.5，这里必须是可见的桩。
                thunkDefs += "static " + returnType + " " + shape.name + "_thunk_" + method.name + "(void* self";
                for (Projected const& projected : params)
                {
                    thunkDefs += ", " + projected.type + " " + projected.name;
                }
                thunkDefs += ")\n{\n";
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
                if (returnType == "uint64_t" &&
                    (hasPrefix(directReturnBare, "castl::weak_ptr") ||
                     hasPrefix(directReturnBare, "std::weak_ptr")))
                {
                    // 弱句柄：登记进同一张表的弱引用槽，`lock()` 失败时 lookup 会置 1001。
                    // 形参要**逐个造出来**（字符串占两个 C ABI 参数）—— 漏了这一步，带参数的
                    // 弱句柄方法会生成 `object->NewWindow()` 这种零实参调用（实测 C2660）。
                    thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    std::string call = "object->" + method.name + "(";
                    std::size_t cursor = 0;
                    bool firstArgument = true;
                    for (auto const& parameter : method.params)
                    {
                        const std::string bare = bareType(parameter.first);
                        const bool stringish = hasPrefix(bare, "castl::string_view") ||
                                               hasPrefix(bare, "std::string_view") ||
                                               bare == "castl::string" || bare == "std::string";
                        std::string argument;
                        if (stringish)
                        {
                            const std::string pointerName =
                                cursor < params.size() ? params[cursor].name : "arg";
                            const std::string lengthName =
                                cursor + 1 < params.size() ? params[cursor + 1].name : "argLen";
                            const bool wantsString = (bare == "castl::string" || bare == "std::string");
                            argument = (wantsString ? "castl::string(" : "castl::string_view(") +
                                       pointerName + ", " + lengthName + ")";
                            cursor += 2;
                        }
                        else
                        {
                            if (cursor >= params.size())
                            {
                                break;
                            }
                            argument = params[cursor].name;
                            cursor += 1;
                        }
                        call += (firstArgument ? "" : ", ") + argument;
                        firstArgument = false;
                    }
                    call += ")";
                    thunkDefs += "    return " + shape.name +
                                 "_registerWeakHandle(std::weak_ptr<void>(" + call + "));\n";
                }
                else if (stringCallable && hasStringParam && returnType == "void" && isDirectReturn)
                {
                    thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    std::string call = "object->" + method.name + "(";
                    std::size_t cursor = 0;
                    bool first = true;
                    for (auto const& parameter : method.params)
                    {
                        const std::string bare = bareType(parameter.first);
                        const bool stringish = hasPrefix(bare, "castl::string_view") ||
                                               hasPrefix(bare, "std::string_view") ||
                                               bare == "castl::string" || bare == "std::string";
                        std::string argument;
                        if (stringish)
                        {
                            const std::string pointerName =
                                cursor < params.size() ? params[cursor].name : "arg";
                            const std::string lengthName =
                                cursor + 1 < params.size() ? params[cursor + 1].name : "argLen";
                            const bool wantsString = (bare == "castl::string" || bare == "std::string");
                            argument = (wantsString ? "castl::string(" : "castl::string_view(") +
                                       pointerName + ", " + lengthName + ")";
                            cursor += 2;
                        }
                        else
                        {
                            if (cursor >= params.size())
                            {
                                break;
                            }
                            const Projected& projected = params[cursor];
                            const bool isOutPointer =
                                !projected.type.empty() && projected.type.back() == '*' &&
                                isScalarType(trimCopy(projected.type.substr(0, projected.type.size() - 1)));
                            argument = isOutPointer ? "*" + projected.name : projected.name;
                            cursor += 1;
                        }
                        call += (first ? "" : ", ") + argument;
                        first = false;
                    }
                    call += ")";
                    thunkDefs += "    " + call + ";\n";
                }
                // 句柄返回：保活 + 登记；句柄参数：反查（失效就置错误码，不崩）
                else if (isHandleReturn)
                {
                    thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    thunkDefs += "    auto result = object->" + method.name + "(";
                    bool firstArgument = true;
                    for (Projected const& projected : params)
                    {
                        thunkDefs += (firstArgument ? "" : ", ") + projected.name;
                        firstArgument = false;
                    }
                    thunkDefs += ");\n";
                    thunkDefs += "    if (!result) { " + shape.name +
                                 "_setError(1002, \"实现返回了空指针\"); return 0u; }\n";
                    thunkDefs += "    return " + shape.name + "_registerStrongHandle(std::shared_ptr<void>(result));\n";
                }
                else if (simpleShape && scalarReturn && isDirectReturn)
                {
                    thunkDefs += "    auto* object = static_cast<" + shape.qualifiedName + "*>(self);\n";
                    std::string call = "object->" + method.name + "(";
                    bool first = true;
                    for (Projected const& projected : params)
                    {
                        // 出参指针：解引用传回去（`*outX`）；标量：原样传
                        const bool isOutPointer = !projected.type.empty() && projected.type.back() == '*' &&
                                                  isScalarType(trimCopy(projected.type.substr(0, projected.type.size() - 1)));
                        std::string argument = isOutPointer ? "*" + projected.name : projected.name;
                        // 枚举参数：接口上收的是 `int32_t`，实现要的是枚举类型，不显式转回去编不过
                        // （实测：`IBindRow01::GetMode(EBindMode)` 收到 `int32_t` 时 C2664）
                        if (!isOutPointer && projected.type == "int32_t")
                        {
                            for (std::size_t pi = 0; pi < method.params.size() && pi < method.paramKinds.size(); ++pi)
                            {
                                if (method.params[pi].second != projected.name)
                                {
                                    continue;
                                }
                                if (method.paramKinds[pi] == static_cast<int>(CXType_Enum))
                                {
                                    // libclang 给的是**非限定**拼写（`EBindMode`），生成物在全局作用域，
                                    // 直接用会 C2061。按接口自己的命名空间补上前缀；枚举已在别的命名空间
                                    // 里写了限定名的话原样用。
                                    std::string enumName = bareType(method.params[pi].first);
                                    if (enumName.find("::") == std::string::npos)
                                    {
                                        const std::size_t split = shape.qualifiedName.rfind("::");
                                        if (split != std::string::npos)
                                        {
                                            enumName = shape.qualifiedName.substr(0, split + 2) + enumName;
                                        }
                                    }
                                    argument = "static_cast<" + enumName + ">(" + argument + ")";
                                }
                                break;
                            }
                        }
                        call += (first ? "" : ", ") + argument;
                        first = false;
                    }
                    call += ")";
                    if (returnType == "void")
                    {
                        thunkDefs += "    " + call + ";\n";
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
                else
                {
                    thunkDefs += "    /* 形状由 2.4 / 2.5 接：容器 / 两段式 / 句柄 / 回调 / 出参 */\n";
                    if (returnType != "void")
                    {
                        thunkDefs += "    return {};   // 零值（标量、指针、镜像结构体都成立）\n";
                    }
                }
                thunkDefs += "}\n\n";

                // 导出入口：`extern "C"` 的那一层只是把参数原样转给 thunk（声明在上面，定义在这里）
                externDefs += "extern \"C\" " + returnType + " " + shape.name + "_" + method.name +
                              "(void* self";
                for (Projected const& projected : params)
                {
                    externDefs += ", " + projected.type + " " + projected.name;
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
            cpp += "\n";
            // ---- 版本化函数表 + 取表函数（任务 2.2）----
            // 校验口径：**版本相等且大小相等**才通过（与已落地的 Scripting/Managed/Host.cs 一致）；
            // 不符时一个字节都不写、返回 0。`GetApi` 把调用方的 size 收进来，就是为了"只写它给的那一段"。
            // 布局清单已在本文件开头发出（镜像类型必须先于声明）
            cpp += "typedef struct " + shape.name + "Api_v1 {\n";
            cpp += "    uint32_t abi_version;\n";
            cpp += "    uint32_t size;\n";
            cpp += tableFields;
            cpp += "} " + shape.name + "Api_v1;\n\n";
            // 句柄表与错误通道的**数据**必须定义在 thunk 之前（thunk 里要用），实现体可以放在后面 ——
            // 这里先把类型、两张表与三个 helper 的前置声明吐出来，避免"先用后定义"。
            cpp += "static const uint32_t " + shape.name + "_HANDLE_INDEX_BITS = " + std::to_string(handleIndexBits) + "u;\n";
            cpp += "static const uint32_t " + shape.name + "_HANDLE_GEN_BITS = " + std::to_string(handleGenerationBits) + "u;\n";
            cpp += "static const int32_t " + shape.name + "_ERROR_STALE_HANDLE = 1001;\n";
            // 句柄表：**一张表**装两种句柄（强引用 / 弱引用）。
            // 旧实现是两张平行容器（`_handles` 按空槽分配 + `_keepAlive` 按下标追加），
            // 两者下标不是一回事，而 `releaseHandle` 只清前者 —— 对象因此永不析构
            // （`WindowSystem` 的回收条件 `use_count() == 1` 永不成立）。合成一张之后，
            // 释放时 `strong` / `weak` 一起清，代际进位，一个动作完成四件事。
            cpp += "typedef struct " + shape.name + "HandleEntry { void const* object; uint32_t generation; "
                   "uint32_t kind; std::shared_ptr<void> strong; std::weak_ptr<void> weak; } " +
                   shape.name + "HandleEntry;\n";
            cpp += "// kind: 0 = 空槽，1 = 强句柄（生成物持有强引用），2 = 弱句柄（只观察，不抬高 use_count）\n";
            // 句柄表**故意不析构**：它是带 `shared_ptr` / `weak_ptr` 的静态容器，进程退出时它会在
            // 别的静态（CRT 的锁与堆）之后才被销毁，实测在 `dynamic atexit destructor` 里
            // ACCESS_VIOLATION（`IWindowSystem_handles` 那条栈）。挂在一块永不释放的堆对象上，
            // 生存期就到进程结束为止 —— 写成"引用绑定到 new"，所有使用处仍是 `X_handles[...]`。
            cpp += "static std::vector<" + shape.name + "HandleEntry>& " + shape.name +
                   "_handles = *new std::vector<" + shape.name + "HandleEntry>();\n";

            // 句柄表的所有读写都在这把锁里（任务 4.3）：egister* / lookupHandle / eleaseHandle。
            // 用递归互斥量是因为 eleaseHandle 内部会调一次 lookupHandle。
            cpp += "static std::recursive_mutex " + shape.name + "_handleMutex;\n";            cpp += "static void " + shape.name + "_setError(int32_t code, char const* text);\n";
            cpp += "static uint64_t " + shape.name + "_registerStrongHandle(std::shared_ptr<void> object);\n";
            cpp += "static uint64_t " + shape.name + "_registerWeakHandle(std::weak_ptr<void> object);\n";
            cpp += "static void* " + shape.name + "_lookupHandle(uint64_t handle);\n";
            cpp += "static void " + shape.name + "_releaseHandle(uint64_t handle);\n\n";
            cpp += thunkDefs;
            cpp += externDefs;
            // 表头（abi_version / size）之后每个成员都是 8 字节对齐的函数指针：偏移 = 8 + 8 × 序号。
            // C# 侧要靠这些常量定位成员，所以同时给出 static_assert 把偏移钉死在编译期。
            {
                // 表里只放**真正生成出来**的方法：带理由排除的那些不在表里（否则 offsetof 指向一个
                // 不存在的成员 —— 实测 IWindow.GetWindowSystem 报 C2039 / C2618）
                std::vector<MethodSignature const*> tableMethods;
                for (MethodSignature const& method : shape.methods)
                {
                    auto skipped = skippedMethods.find(shape.name);
                    if (skipped != skippedMethods.end() && skipped->second.count(method.name) > 0)
                    {
                        continue;
                    }
                    std::vector<Projected> probeParams;
                    std::string probeReturn;
                    std::string probeReason;
                    bool probeTwoStep = false;
                    bool probeHandle = false;
                    if (!projectMethod(method, &probeParams, &probeReturn, &probeReason, &probeTwoStep,
                                       &probeHandle))
                    {
                        continue;
                    }
                    // 两段式（字符串返回）不进函数表：这里也必须排除，否则下方按 tableMethods
                    // 生成的 `static_assert(offsetof(...))` 会引用一个不存在的成员（实测 C2039）。
                    if (probeTwoStep)
                    {
                        const std::string probeBare = bareType(method.returnSpelling);
                        if (hasPrefix(probeBare, "castl::string_view") ||
                            hasPrefix(probeBare, "std::string_view") ||
                            probeBare == "castl::string" || probeBare == "std::string")
                        {
                            continue;
                        }
                    }
                    tableMethods.push_back(&method);
                }
                unsigned memberIndex = 0;
                cs += "        // 函数表布局（与 C++ 侧同源常量；不一致由 C++ 的 static_assert 先兜住）\n";
                cs += "        public const int " + shape.name + "Api_v1_Size = 8 + 8 * " +
                      std::to_string(tableMethods.size()) + ";\n";
                for (MethodSignature const* method : tableMethods)
                {
                    cpp += "static_assert(offsetof(" + shape.name + "Api_v1, " + method->name + ") == " +
                           std::to_string(8u + 8u * memberIndex) + ", \"函数表成员偏移变了：\" \"" +
                           shape.name + "." + method->name + "\");\n";
                    cs += "        public const int " + shape.name + "Api_v1_" + method->name + " = " +
                          std::to_string(8u + 8u * memberIndex) + ";\n";
                    ++memberIndex;
                }
                cpp += "\n";
            }
            cpp += "static " + shape.name + "Api_v1 const " + shape.name + "_api = {\n";
            cpp += "    1u, (uint32_t)sizeof(" + shape.name + "Api_v1),\n";
            cpp += tableInit;
            cpp += "};\n\n";
            cpp += "extern \"C\" uint32_t " + shape.name + "_GetApi(uint32_t version, uint32_t size, " +
                   shape.name + "Api_v1* out)\n{\n";
            cpp += "    const uint32_t realSize = (uint32_t)sizeof(" + shape.name + "Api_v1);\n";
            cpp += "    if (version != 1u || size != realSize || out == nullptr)\n    {\n        return 0u;\n    }\n";
            cpp += "    ::memcpy(out, &" + shape.name + "_api, realSize);\n";
            cpp += "    return realSize;\n}\n\n";
            // ---- 错误通道 + 句柄表（任务 2.4）----
            // 句柄 = 低 <INDEX> 位序号 + 高 <GEN> 位代际（位数在 2.9 变成参数，这里先用默认 48/16）。
            // `Release` 让代际进位 ⇒ 旧句柄立刻失效；失效时置错误码 1001 并留一条文本。
            cpp += "static int32_t " + shape.name + "_lastErrorCode = 0;\n";
            cpp += "static char " + shape.name + "_lastErrorText[256] = { 0 };\n";
            cpp += "static void " + shape.name + "_setError(int32_t code, char const* text)\n{\n";
            cpp += "    " + shape.name + "_lastErrorCode = code;\n";
            cpp += "    if (text != nullptr)\n    {\n";
            cpp += "        ::memcpy(" + shape.name + "_lastErrorText, text, ::strlen(text) + 1u);\n";
            cpp += "    }\n}\n";
            cpp += "extern \"C\" int32_t " + shape.name + "_LastErrorCode(void*)\n{\n    return " +
                   shape.name + "_lastErrorCode;\n}\n";
            cpp += "extern \"C\" int32_t " + shape.name + "_LastErrorText(void*, char* dst, uint64_t cap)\n{\n";
            cpp += "    const std::size_t length = ::strlen(" + shape.name + "_lastErrorText);\n";
            cpp += "    const std::size_t copied = (cap > 0u) ? ((length < cap - 1u) ? length : cap - 1u) : 0u;\n";
            cpp += "    if (cap > 0u)\n    {\n        ::memcpy(dst, " + shape.name +
                   "_lastErrorText, copied);\n        dst[copied] = 0;\n    }\n    return (int32_t)copied;\n}\n\n";
            cpp += "static uint64_t " + shape.name + "_registerStrongHandle(std::shared_ptr<void> object)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + shape.name + "_handleMutex);\n";
            cpp += "    if (!object) { " + shape.name + "_setError(1002, \"实现返回了空指针\"); return 0u; }\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    uint64_t index = 1u;\n";
            cpp += "    while (index <= indexMask && index < " + shape.name + "_handles.size() + 1u)\n    {\n";
            cpp += "        if (" + shape.name + "_handles[index - 1u].kind == 0u) { break; }\n";
            cpp += "        ++index;\n    }\n";
            cpp += "    if (index > indexMask) { return 0u; }\n";
            cpp += "    if (index > " + shape.name + "_handles.size())\n    {\n        " + shape.name +
                   "_handles.push_back({});\n    }\n";
            cpp += "    auto& entry = " + shape.name + "_handles[index - 1u];\n";
            cpp += "    entry.object = object.get();\n    entry.kind = 1u;\n    entry.strong = object;\n";
            cpp += "    entry.weak.reset();\n";
            cpp += "    return (static_cast<uint64_t>(entry.generation) << " +
                   shape.name + "_HANDLE_INDEX_BITS) | index;\n}\n";
            cpp += "static uint64_t " + shape.name + "_registerWeakHandle(std::weak_ptr<void> object)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + shape.name + "_handleMutex);\n";
            cpp += "    if (object.expired()) { " + shape.name + "_setError(1002, \"实现返回了空指针\"); return 0u; }\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    uint64_t index = 1u;\n";
            cpp += "    while (index <= indexMask && index < " + shape.name + "_handles.size() + 1u)\n    {\n";
            cpp += "        if (" + shape.name + "_handles[index - 1u].kind == 0u) { break; }\n";
            cpp += "        ++index;\n    }\n";
            cpp += "    if (index > indexMask) { return 0u; }\n";
            cpp += "    if (index > " + shape.name + "_handles.size())\n    {\n        " + shape.name +
                   "_handles.push_back({});\n    }\n";
            cpp += "    auto& entry = " + shape.name + "_handles[index - 1u];\n";
            // 弱句柄不持有强引用，所以 object 先写 weak 换出来的指针；对象一死 lookup 就失败
            cpp += "    entry.object = object.lock().get();\n    entry.kind = 2u;\n    entry.weak = object;\n";
            cpp += "    entry.strong.reset();\n";
            cpp += "    return (static_cast<uint64_t>(entry.generation) << " +
                   shape.name + "_HANDLE_INDEX_BITS) | index;\n}\n";
            cpp += "static void* " + shape.name + "_lookupHandle(uint64_t handle)\n{\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            cpp += "    const uint64_t generation = handle >> " + shape.name + "_HANDLE_INDEX_BITS;\n";
            cpp += "    if (index == 0u || index > " + shape.name + "_handles.size()) { return nullptr; }\n";
            cpp += "    auto& entry = " + shape.name + "_handles[index - 1u];\n";
            cpp += "    /* kind 3 = 已退休的槽：代际回绕过，永不再分配 */\n    if (entry.kind == 0u || entry.kind == 3u || entry.generation != generation) { return nullptr; }\n";
            // 弱句柄：每次取用都 lock 一次，对象已析构就判失效（1001）
            cpp += "    if (entry.kind == 2u)\n    {\n";
            cpp += "        auto locked = entry.weak.lock();\n";
            cpp += "        if (!locked) { " + shape.name + "_setError(" + shape.name +
                   "_ERROR_STALE_HANDLE, \"stale handle\"); return nullptr; }\n";
            cpp += "        return locked.get();\n    }\n";
            cpp += "    return const_cast<void*>(entry.object);\n}\n";
            cpp += "static void " + shape.name + "_releaseHandle(uint64_t handle)\n{\n";
            cpp += "    std::lock_guard<std::recursive_mutex> guard(" + shape.name + "_handleMutex);\n";
            cpp += "    const uint64_t indexMask = (1ull << " + shape.name + "_HANDLE_INDEX_BITS) - 1ull;\n";
            cpp += "    const uint64_t index = handle & indexMask;\n";
            cpp += "    if (" + shape.name + "_lookupHandle(handle) == nullptr)\n    {\n        " + shape.name +
                   "_setError(" + shape.name + "_ERROR_STALE_HANDLE, \"stale handle\");\n        return;\n    }\n";
            cpp += "    auto& entry = " + shape.name + "_handles[index - 1u];\n";
            // 释放 = 表项清空 + 强/弱引用一起放掉 + 代际进位（旧句柄因此被判 stale）
            cpp += "    entry.kind = 0u;\n    entry.object = nullptr;\n";
            cpp += "    entry.strong.reset();\n    entry.weak.reset();\n";
            // 代际是 16 位、会回绕（任务 4.1）：同一个槽被登记/释放 2^16 次之后，代际回到 0，
            // 最早那个句柄就会重新"有效"。代际一旦要归零就把槽**退休**（kind = 3，永不再分配），
            // 宁可少一个槽，也不要让旧句柄复活。
            cpp += "    const uint32_t nextGeneration = (entry.generation + 1u) & ((1u << " + shape.name +
                   "_HANDLE_GEN_BITS) - 1u);\n";
            cpp += "    if (nextGeneration == 0u) { entry.kind = 3u; }\n";
            cpp += "    entry.generation = nextGeneration;\n";
            cpp += "    " + shape.name + "_lastErrorCode = 0;\n}\n\n";
            // 每接口两个泛用入口（任务 1.3）：`ResolveHandle` 是必需的 —— C ABI 面所有方法入口
            // 的第一个参数都是 `void* self`，没有任何一个收定宽句柄；没有它，C# 拿到句柄之后
            // 没有任何办法用它调方法，"旧句柄报 1001" 这条验收也写不出来。
            cpp += "extern \"C\" void " + shape.name + "_ReleaseHandle(void* self, uint64_t handle)\n{\n";
            cpp += "    (void)self;\n    " + shape.name + "_releaseHandle(handle);\n}\n\n";
            cpp += "extern \"C\" void* " + shape.name + "_ResolveHandle(uint64_t handle)\n{\n";
            cpp += "    return " + shape.name + "_lookupHandle(handle);\n}\n\n";
            cpp += lateDefs;
            cs += "    }\n}\n";
            methodsTotal += emitted;
            if (emitted == 0)
            {
                continue;
            }

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

        std::fprintf(stderr, "绑定：接口 %zu 个 / 方法 %d 条 / 产物 %d 个 / 拒绝 %d 条\n",
                     shapes.size(), methodsTotal, products, rejected);
        if (rejected > 0)
        {
            return 1;
        }
        // 没有产物不等于失败：整个接口的方法都被带理由排除掉时，0 个产物是正确的
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

    // 绑定发射器走自己的分支：它按 `CA_BIND` 扫描整个头、一次出多份产物，不需要 <TypeName> 参数。
    // 放在 `--emit-all` 的一致性检查之前，因为那几条检查只对反射/序列化/哈希三节有意义。
    if (emitBinding)
    {
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