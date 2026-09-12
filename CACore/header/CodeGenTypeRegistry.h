#pragma once
//
// CodeGenTypeRegistry.h — OPTIONAL runtime type registry for the CodeGen metadata model (design D3,
// task 5.2). Resolves a runtime type-name string to that type's linear CodeGenTypeInfo, for
// inspector / scripting tools that need to introspect a type by name. Mirrors the by-name resolution
// idiom of IModuleManager::GetInstance(NameHash) — a key→pointer dictionary that returns null on a
// miss — but keyed by plain string so it stays a self-contained optional helper (NOT the module
// system, and NOT a hot path). Include this header only in TUs that want the registry.
//
#include "CodeGenMeta.h"

#include <cstddef>
#include <cstring>

namespace cacore::codegen
{
    class TypeRegistry
    {
    public:
        static constexpr std::size_t kMaxTypes = 512;

        // Register a type's linear metadata under its runtime name. Fail-safe: ignore null / overflow.
        static void registerType(const char* name, const CodeGenTypeInfo* info)
        {
            if (!name || !info || s_count >= kMaxTypes)
                return;
            s_names[s_count] = name;
            s_infos[s_count] = info;
            ++s_count;
        }

        // name -> metadata; returns nullptr if not registered (the "未注册名返回空" negative oracle).
        static const CodeGenTypeInfo* find(const char* name) noexcept
        {
            for (std::size_t i = 0; i < s_count; ++i)
                if (s_names[i] && std::strcmp(s_names[i], name) == 0)
                    return s_infos[i];
            return nullptr;
        }

        static void clear() noexcept { s_count = 0; }

        // Register an aggregate T from its emitter-produced TypeDescriptor<T>. Builds a persistent
        // CodeGenTypeInfo on first call (one per T), so find(name) yields a stable, readable descriptor.
        template<typename T>
        static void registerTypeDescriptor()
        {
            static CodeGenTypeInfo info{
                TypeDescriptor<T>::name,
                TypeKind::Struct,
                TypeDescriptor<T>::fieldCount,
                TypeDescriptor<T>::fields(),
            };
            registerType(TypeDescriptor<T>::name, &info);
        }

    private:
        inline static const char*           s_names[kMaxTypes]  = {};
        inline static const CodeGenTypeInfo* s_infos[kMaxTypes] = {};
        inline static std::size_t           s_count             = 0;
    };
} // namespace cacore::codegen
