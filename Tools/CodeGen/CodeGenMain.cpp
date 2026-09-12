//
// CodeGenMain.cpp — minimal libclang-driven extraction tool for the CodeGen framework.
//
// Takes a header file + a type name, parses it with libclang (the real C++ front end — we are
// NOT hand-writing a parser), finds the struct/class of that name, and emits a deterministic
// linear field table (one line per field, sorted by byte offset):
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
    };

    struct CollectCtx
    {
        std::vector<std::pair<std::string, CXCursor>>* out;
    };

    enum CXChildVisitResult collectStructs(CXCursor cursor, CXCursor /*parent*/, CXClientData data)
    {
        auto* ctx  = static_cast<CollectCtx*>(data);
        CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl)
        {
            CXString spelling = clang_getCursorSpelling(cursor);
            ctx->out->emplace_back(clang_getCString(spelling), cursor);
            clang_disposeString(spelling);
            return CXChildVisit_Continue; // do not recurse into nested decls (top-level sufficient)
        }
        return CXChildVisit_Recurse;
    }

    enum CXChildVisitResult collectFields(CXCursor cursor, CXCursor /*parent*/, CXClientData data)
    {
        auto* fields = static_cast<std::vector<Field>*>(data);
        if (clang_getCursorKind(cursor) == CXCursor_FieldDecl)
        {
            CXString sName = clang_getCursorSpelling(cursor);
            CXType   type  = clang_getCursorType(cursor);
            CXString sType = clang_getTypeSpelling(type);
            long long bits = clang_Cursor_getOffsetOfField(cursor);

            Field f;
            f.name       = clang_getCString(sName);
            f.typeName   = clang_getCString(sType);
            f.offsetBytes = bits > 0 ? static_cast<std::size_t>(bits) / 8u : 0u;
            f.typeKind   = static_cast<int>(type.kind);

            clang_disposeString(sName);
            clang_disposeString(sType);
            fields->push_back(std::move(f));
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

    std::string scanMarkerOptions(const std::string& text, const std::string& typeName)
    {
        const char* marker = "CA_REFLECT(";
        const std::size_t markerLen = std::strlen(marker);
        std::string currentMarker;
        std::size_t pos = 0;
        while (pos < text.size())
        {
            std::size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            std::string l = line; // keep raw for marker lookup on any column
            // marker present?
            std::size_t m = l.find(marker);
            if (m != std::string::npos)
            {
                std::size_t open = m + markerLen;
                std::size_t close = l.find(')', open);
                if (close != std::string::npos)
                    currentMarker = trim(l.substr(open, close - open));
            }
            // type declaration reached (nearest preceding marker wins)?
            if (l.find("struct " + typeName) != std::string::npos ||
                l.find("class " + typeName) != std::string::npos)
            {
                return trim(currentMarker);
            }
            if (nl == std::string::npos)
                break;
            pos = nl + 1;
        }
        return {};
    }

    // --- Task 6b.3: CA_REFLECTION(Type, a, b) macro -> fields-ONLY (declaration-order member names).
    // This is the "unmigrated-engine + external" transition channel the design D2 calls the
    // non-invasive path: it never edits the type, and only the macro's member list is available
    // (no types/offsets — the generated code resolves those later via offsetof). First arg is the
    // type name; remaining args are the member names in declaration order.
    std::vector<std::string> scanCA_REFLECTIONList(const std::string& text, const std::string& typeName)
    {
        const char* marker = "CA_REFLECTION(";
        const std::size_t markerLen = std::strlen(marker);
        std::vector<std::string> members;
        std::size_t pos = 0;
        while (pos < text.size())
        {
            std::size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            std::size_t m = line.find(marker);
            if (m != std::string::npos)
            {
                std::size_t open = m + markerLen;
                std::size_t close = line.find(')', open);
                if (close != std::string::npos)
                {
                    std::string inner = trim(line.substr(open, close - open)); // "Type, a, b"
                    std::vector<std::string> parts;
                    std::string cur;
                    for (char c : inner)
                    {
                        if (c == ',') { parts.push_back(trim(cur)); cur.clear(); }
                        else cur += c;
                    }
                    parts.push_back(trim(cur));
                    if (!parts.empty() && parts[0] == typeName)
                    {
                        for (std::size_t i = 1; i < parts.size(); ++i)
                            members.push_back(parts[i]);
                        return members; // first matching declaration wins
                    }
                }
            }
            if (nl == std::string::npos)
                break;
            pos = nl + 1;
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
    };

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
    // readers consume). Deterministic: field order is byte-offset-sorted upstream, so the same input
    // always yields the same bytes.
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
            std::fprintf(out, "\n    static constexpr const CodeGenFieldInfo* fields() { return arr; }\n");
            std::fprintf(out, "    static constexpr CodeGenFieldInfo arr[%zu] = {\n", fields.size());
            for (const EmitField& f : fields)
            {
                if (f.typeName.empty())
                    std::fprintf(out,
                        "        { \"%s\", nullptr, offsetof(%s, %s), %s, FieldFlags::HashInclude },\n",
                        f.name.c_str(), qname.c_str(), f.name.c_str(), f.kindName.c_str());
                else
                    std::fprintf(out,
                        "        { \"%s\", \"%s\", offsetof(%s, %s), %s, FieldFlags::HashInclude },\n",
                        f.name.c_str(), f.typeName.c_str(), qname.c_str(), f.name.c_str(), f.kindName.c_str());
            }
            std::fprintf(out, "    };\n");
        }
        std::fprintf(out, "};\n");
        std::fprintf(out, "} // namespace cacore::codegen\n");
    }

    // --- Task 6.1: serialization emitter. Generates a `CodeGenSerializer<T>` that (de)serializes the
    // reflected type field-by-field in DECLARATION ORDER (the offset-sorted order the type table uses,
    // which matches CA_REFLECTION's member order). It is a PARALLEL, data-driven exit distinct from the
    // runtime `careflection`/`visit_members` path (which Serialization.h keeps using). Gated on the
    // `serializable` marker option (design AUDIT-4, decided 2026-09-08): only emitted for serializable
    // types, so `serializable` is actually consumed here. Fragments, like the reflection emitter — the
    // consumer TU must have CodeGenMeta.h + Serialization.h on the include path, and the type defined.
    void emitCodeGenSerializer(const std::vector<EmitField>& fields, const std::string& qname, FILE* out)
    {
        std::fprintf(out,
            "// Generated by CodeGen serialization emitter for %s.\n"
            "// Requires: the type is defined + CodeGenMeta.h + Serialization.h on the include path.\n"
            "// Deterministic: declaration-order field traversal, same input => same bytes.\n"
            "#pragma once\n"
            "\n"
            "#include \"CodeGenMeta.h\"\n"
            "#include \"Serialization.h\"\n"
            "\n",
            qname.c_str());
        std::fprintf(out, "namespace cacore::codegen\n{\n");
        std::fprintf(out, "template<typename T> struct CodeGenSerializer;  // primary template\n");
        std::fprintf(out, "\n");
        std::fprintf(out, "template<>\n");
        std::fprintf(out, "struct CodeGenSerializer<%s>\n{\n", qname.c_str());
        std::fprintf(out, "    using Obj = %s;\n", qname.c_str());
        std::fprintf(out,
            "    template<typename ByteBuffer>\n"
            "    static void serialize(Obj const& obj, ByteBuffer& buffer)\n"
            "    {\n"
            "        ::cacore::serializer<ByteBuffer> s(buffer);  // one instance carries offset\n");
        for (const EmitField& f : fields)
            std::fprintf(out, "        s.serialize(obj.%s);\n", f.name.c_str());
        std::fprintf(out,
            "    }\n"
            "    template<typename ByteBuffer>\n"
            "    static void deserialize(Obj& obj, ByteBuffer const& buffer)\n"
            "    {\n"
            "        ::cacore::deserializer<ByteBuffer> d(buffer);  // one instance carries offset\n");
        for (const EmitField& f : fields)
            std::fprintf(out, "        d.deserialize(obj.%s);\n", f.name.c_str());
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
            "\n",
            qname.c_str());
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
            if (f.kindName == "TypeKind::Float")
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

    std::vector<std::string> buildWhitelist(const std::string& text)
    {
        std::vector<std::string> wl;
        std::size_t pos = 0;
        while (pos < text.size())
        {
            std::size_t nl = text.find('\n', pos);
            const std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);

            // (a) CA_CODEGEN_REGISTER("QualifiedName")
            {
                const char* mk = "CA_CODEGEN_REGISTER(";
                std::size_t m = line.find(mk);
                if (m != std::string::npos)
                {
                    std::size_t open = m + std::strlen(mk);
                    std::size_t close = line.find(')', open);
                    if (close != std::string::npos)
                    {
                        std::string arg = line.substr(open, close - open);
                        std::size_t q1 = arg.find('"');
                        if (q1 != std::string::npos)
                        {
                            std::size_t q2 = arg.find('"', q1 + 1);
                            if (q2 != std::string::npos)
                                wl.push_back(arg.substr(q1 + 1, q2 - q1 - 1));
                        }
                    }
                }
            }

            // (b) CA_REFLECT(...) -- associated type is the struct/class body AFTER this marker line.
            {
                const char* mk = "CA_REFLECT(";
                std::size_t m = line.find(mk);
                if (m != std::string::npos)
                {
                    std::size_t p = nl == std::string::npos ? text.size() : nl + 1;
                    while (p < text.size())
                    {
                        std::size_t n2 = text.find('\n', p);
                        const std::string l2 = text.substr(p, n2 == std::string::npos ? std::string::npos : n2 - p);
                        for (const char* kw : { "struct", "class" })
                        {
                            std::size_t k = l2.find(kw);
                            if (k != std::string::npos)
                            {
                                std::size_t s = k + std::strlen(kw);
                                while (s < l2.size() && (l2[s] == ' ' || l2[s] == '\t')) ++s;
                                std::size_t e = s;
                                while (e < l2.size() &&
                                       (std::isalnum(static_cast<unsigned char>(l2[e])) || l2[e] == '_' || l2[e] == ':'))
                                    ++e;
                                if (e > s)
                                    wl.push_back(l2.substr(s, e - s));
                                break; // one type name per matched struct/class line
                            }
                        }
                        if (n2 == std::string::npos) break;
                        p = n2 + 1;
                    }
                }
            }

            // (c) CA_REFLECTION(Type, a, b)
            {
                const char* mk = "CA_REFLECTION(";
                std::size_t m = line.find(mk);
                if (m != std::string::npos)
                {
                    std::size_t open = m + std::strlen(mk);
                    std::size_t close = line.find(')', open);
                    if (close != std::string::npos)
                    {
                        std::string inner = trim(line.substr(open, close - open));
                        std::size_t com = inner.find(',');
                        std::string typ = com == std::string::npos ? inner : trim(inner.substr(0, com));
                        if (!typ.empty())
                            wl.push_back(typ);
                    }
                }
            }

            if (nl == std::string::npos) break;
            pos = nl + 1;
        }

        // Dedup, preserving first occurrence.
        std::vector<std::string> out;
        for (const std::string& n : wl)
            if (std::find(out.begin(), out.end(), n) == out.end())
                out.push_back(n);
        return out;
    }
} // namespace

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
    const std::vector<std::string> whitelist = buildWhitelist(source);
    if (whitelist.empty())
    {
        std::fprintf(stderr, "CodeGen: no annotated types (empty whitelist) in '%s'; nothing to generate\n", filePath);
        return 0; // spec.md "空白名单 -> EXIT 0 且不生成文件": no file opened, nothing emitted.
    }
    const bool targetWhitelisted = isWhitelisted(typeName, whitelist) ||
                                   (!qualifiedNameOverride.empty() && isWhitelisted(qualifiedNameOverride, whitelist));
    if (!targetWhitelisted)
    {
        std::fprintf(stderr,
            "CodeGen: type '%s' is not annotated (use CA_REFLECT / CA_CODEGEN_REGISTER / CA_REFLECTION); refusing to extract\n",
            typeName);
        return 4; // negative: an unmarked struct in a header that HAS marked types is not extracted.
    }

    FILE* out = outPath ? std::fopen(outPath, "w") : stdout;
    if (!out)
    {
        std::fprintf(stderr, "CodeGen: cannot open output '%s'\n", outPath);
        return 2;
    }
    const bool outIsFile = outPath != nullptr;
    // Lambda so we close the file exactly once on every emit path (stdout stays open).
    auto closeOut = [&] { if (outIsFile) std::fclose(out); };

    // Task 6b.3 dual-mode: if the type is declared via `CA_REFLECTION(Type, a, b)` (the
    // unmigrated-engine + external transition channel), emit a fields-ONLY declaration-order table
    // from the macro's member list. This never edits the type and needs no libclang.
    {
        std::vector<std::string> members = scanCA_REFLECTIONList(source, typeName);
        if (!members.empty())
        {
            const std::string qname = qualifiedNameOverride.empty() ? typeName : qualifiedNameOverride;
            if (emitReflection)
            {
                // For this channel only the member NAMES are known, so offset comes from offsetof at
                // compile time and the type/kind are genuinely unknown (TypeKind::None + nullptr
                // typeName). Honest: the CA_REFLECTION macro carries no type/cursor info (design D2).
                std::vector<EmitField> ef;
                for (const std::string& m : members)
                    ef.push_back({ m, {}, "TypeKind::None" });
                emitTypeDescriptor(ef, qname, out);
                closeOut();
                return 0;
            }
            std::printf("%s %zu\n", typeName, members.size());
            for (const std::string& m : members)
                std::printf("  field %s\n", m.c_str());
            return 0;
        }
    }

    std::vector<const char*> args;
    args.push_back("-xc++"); // treat input as C++, not inferred by extension
    args.push_back("-std=c++20");
    for (const char* a : clangArgs)
        args.push_back(a);

    CXIndex index = clang_createIndex(/*excludeDeclsFromPCH*/ 1, /*displayDiagnostics*/ 0);
    CXTranslationUnit tu = nullptr;
    CXErrorCode err = clang_parseTranslationUnit2(
        index, filePath, args.data(), static_cast<int>(args.size()),
        /*unsaved_files*/ nullptr, /*num_unsaved*/ 0, CXTranslationUnit_None, &tu);

    if (err != CXError_Success)
    {
        std::fprintf(stderr, "libclang: parse failed (error=%d) for '%s'\n", static_cast<int>(err), filePath);
        clang_disposeIndex(index);
        return 1;
    }

    // Find the struct/class by the (last, unqualified) spelling. Enough for a first proof where the
    // whitelisted type name is unique; later may switch to USR/qualified matching for disambiguation.
    std::vector<std::pair<std::string, CXCursor>> structs;
    CollectCtx collect{ &structs };
    clang_visitChildren(clang_getTranslationUnitCursor(tu), collectStructs, &collect);

    CXCursor target{};
    bool found = false;
    for (auto& pr : structs)
    {
        if (pr.first == typeName)
        {
            target = pr.second;
            found  = true;
            break;
        }
    }
    if (!found)
    {
        std::fprintf(stderr, "libclang: type '%s' not found in '%s'\n", typeName, filePath);
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 3;
    }

    std::vector<Field> fields;
    clang_visitChildren(target, collectFields, &fields);

    // Deterministic output: declaration order via visitChildren is already deterministic; sorting by
    // offset additionally guards against any iteration reordering across runs.
    std::sort(fields.begin(), fields.end(),
              [](const Field& a, const Field& b) { return a.offsetBytes < b.offsetBytes; });

    // Task 5.1 reflection / Task 6.1 serialization emitter: the qualified name comes from the AST
    // (or an explicit --qualified-name override) — the CA_REFLECT marker has no type name, so the tool
    // derives it from where the type actually lives. Both share the per-field metadata.
    const std::string qname =
        qualifiedNameOverride.empty() ? buildQualifiedName(target) : qualifiedNameOverride;
    std::vector<EmitField> ef;
    for (const Field& f : fields)
        ef.push_back({ f.name, f.typeName, fieldTypeKindEnum(static_cast<CXTypeKind>(f.typeKind)) });
    if (emitSerialization)
    {
        emitCodeGenSerializer(ef, qname, out);
        closeOut();
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 0;
    }
    if (emitHash)
    {
        emitHashTrait(ef, qname, out);
        closeOut();
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 0;
    }
    if (emitReflection)
    {
        emitTypeDescriptor(ef, qname, out);
        closeOut();
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return 0;
    }

    std::printf("%s %zu\n", typeName, fields.size());
    for (const Field& f : fields)
        std::printf("  field %s %zu %s %d\n", f.name.c_str(), f.offsetBytes, f.typeName.c_str(), f.typeKind);

    // Task 6b.2: carry CA_REFLECT marker options (from source text) into the output.
    const std::string options = scanMarkerOptions(source, typeName);
    if (!options.empty())
        std::printf("# options: %s\n", options.c_str());

    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    return 0;
}
