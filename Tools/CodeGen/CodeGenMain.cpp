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
    const char* outPath           = nullptr;
    const char* outDir            = nullptr;
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
        else if (a == "--emit-all")
        {
            emitAll = true;
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
