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
            f.typeKind   = static_cast<int>(type.kind);

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

    /** Task 9.59, honest boundary: the CA_REFLECTION channel stays a TEXT channel.
        `CA_REFLECTION(Type, a, b)` is not a macro in this tool's world — CACore/header/CodeGenMeta.h
        defines only CA_REFLECT and CA_CODEGEN_REGISTER, and the channel exists precisely for library
        types whose annotation lives in a header the tool does not compile (design D2: "the tool reads
        this macro's member list from the source text"). The preprocessor therefore cannot report an
        invocation, so this ONE channel still parses text — with comments and string/char literal
        interiors blanked (task 9.57), which is what keeps a spelling inside a string or a comment from
        registering a type. Residual, documented limit: a CA_REFLECTION spelling inside a DISABLED
        preprocessor branch is still read as an annotation. Only this channel has that limit; the
        CA_REFLECT and CA_CODEGEN_REGISTER decisions are taken from the preprocessor. */
    std::string blankCommentsAndLiterals(const std::string& text)
    {
        std::string out = text;
        bool inLine = false, inBlock = false;
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            const char c = out[i];
            const char n = (i + 1 < out.size()) ? out[i + 1] : '\0';
            if (inLine)
            {
                if (c == '\\' && (n == '\n' || n == '\r')) { out[i] = ' '; continue; }
                if (c == '\n') inLine = false;
                else if (c != '\r') out[i] = ' ';
                continue;
            }
            if (inBlock)
            {
                if (c == '*' && n == '/') { out[i] = out[i + 1] = ' '; ++i; inBlock = false; }
                else if (c != '\n' && c != '\r') out[i] = ' ';
                continue;
            }
            if (c == '/' && n == '/') { out[i] = out[i + 1] = ' '; ++i; inLine = true; continue; }
            if (c == '/' && n == '*') { out[i] = out[i + 1] = ' '; ++i; inBlock = true; continue; }
            if (c == '"' || c == '\'')
            {
                std::size_t j = i + 1;
                for (; j < out.size(); ++j)
                {
                    if (out[j] == '\\') { ++j; continue; }
                    if (out[j] == c || out[j] == '\n') break;
                }
                for (std::size_t k = i + 1; k < j && k < out.size(); ++k)
                    if (out[k] != '\n' && out[k] != '\r') out[k] = ' ';
                i = (j < out.size()) ? j : out.size() - 1;
            }
        }
        return out;
    }

    /** The CA_REFLECTION(Type, a, b) declarations in a file, from the blanked view above.
        Returns (type name as written, argument text after it). */
    std::vector<std::pair<std::string, std::string>> scanCA_REFLECTIONUses(const std::string& code)
    {
        const char* mk = "CA_REFLECTION(";
        const std::size_t mkLen = std::strlen(mk);
        std::vector<std::pair<std::string, std::string>> out;
        for (std::size_t m = code.find(mk); m != std::string::npos; m = code.find(mk, m + mkLen))
        {
            const std::size_t open = m + mkLen;
            int depth = 1;
            std::size_t i = open;
            for (; i < code.size() && depth > 0; ++i)
            {
                if (code[i] == '(') ++depth;
                else if (code[i] == ')') --depth;
            }
            if (depth != 0)
                continue; // a malformed one is skipped, not fatal for the others
            const std::string args = trim(code.substr(open, i - 1 - open));
            const std::size_t comma = args.find(',');
            const std::string first = trim(comma == std::string::npos ? args : args.substr(0, comma));
            if (!first.empty())
                out.emplace_back(first, args);
        }
        return out;
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
    // reflected type field-by-field in DECLARATION ORDER (the order the type table carries,
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
    std::map<std::string, AnnotationBinding> bound = bindAnnotations(uses, occurrences);
    // The one channel the preprocessor cannot report (see blankCommentsAndLiterals): CA_REFLECTION is not
    // a macro here, so its uses are read from the blanked text and merged into the same binding map. The
    // text is blanked first, so a spelling inside a comment or a string literal does not register a type.
    {
        const std::string code = blankCommentsAndLiterals(source);
        for (const auto& u : scanCA_REFLECTIONUses(code))
            if (bound.find(u.first) == bound.end())
                bound[u.first] = { "CA_REFLECTION", u.second, CXCursor{}, false };
    }
    if (bound.empty())
    {
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
        const std::string want     = baseName(filePath);
        const unsigned    n        = clang_getNumDiagnostics(tu);
        unsigned          inHeader = 0;
        std::string       fatalMsg;
        for (unsigned i = 0; i < n; ++i)
        {
            CXDiagnostic d   = clang_getDiagnostic(tu, i);
            const int    sev = static_cast<int>(clang_getDiagnosticSeverity(d));
            if (sev >= CXDiagnostic_Fatal && fatalMsg.empty())
            {
                CXString msg = clang_formatDiagnostic(d, clang_defaultDiagnosticDisplayOptions());
                const char* c = clang_getCString(msg);
                fatalMsg = (c != nullptr) ? c : "(unformattable)";
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
                    if (same) ++inHeader;
                }
            }
            clang_disposeDiagnostic(d);
        }
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

    std::vector<EmitField> ef;
    for (const Field& f : fields)
        ef.push_back({ f.name, f.typeName, fieldTypeKindEnum(static_cast<CXTypeKind>(f.typeKind)) });
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
        std::printf("  field %s %zu %s %d\n", f.name.c_str(), f.offsetBytes, f.typeName.c_str(), f.typeKind);

    // Task 6b.2: carry CA_REFLECT marker options (from source text) into the output.
    const std::string options = scanMarkerOptions(bound, typeName);
    if (!options.empty())
        std::printf("# options: %s\n", options.c_str());

    abortOut(); // no --emit-*: the table went to stdout, so the output path stays untouched
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    return 0;
}
