#pragma once
//
// CodeGenHashFwd.h — 哈希运行时的**声明部分**：只有特化模板与特化检测这两样。
//
// 给"需要一个类型的哈希走哪条分派分支"的文件用（例如旧设施的 `Hasher.h`），
// 它不必把整套哈希 / 序列化运行时（容器、缓冲区、算法与容器头）拖进 include 图。
// 定义与容器规则在 CodeGenHash.h，宿主类型自己的特化写在类型旁边。

#include "CodeGenTraits.h"
#include <type_traits>

namespace cacore::codegen
{
    // 生成物给出的哈希特化模板（主模板是空的，生成物或宿主给特化）。
    template<typename T>
    struct custom_hash_trait {};

    // 特化检测：某个类型有没有可用的哈希特化。
    template<typename T, typename hasher_t>
    concept has_custom_hash_func = requires(T t, hasher_t h)
    {
        custom_hash_trait<T>::hash(t, h);
    };
}

// 「这个类型按整块字节算哈希」的**显式声明**。写一行就够：自家类型写在类型自己的头里，第三方类型写在模块的
// Includes 头里（范例：`D3D12RenderBackend/private/D3D12Includes.h`、`VulkanRenderBackendNew/private/Utils/
// VulkanIncludes.h` 里那两处）。
//
// ⚠ 写在**全局作用域**（某个 namespace 之外）：宏体里要开 `namespace cacore::codegen { … }`，写在别人的
// namespace 里会把它嵌进去，特化就落错地方了。
//
// ⚠ 只有"没有填充字节、也没有多种表示的成员"才成立：含 `float` / `double` 的类型会被下面那条断言挡住
// （`-0.0` 与 `+0.0` 字节不同、NaN 有多种表示，而字段级那条路会把它们归一）—— 那种类型应该进接入清单。
#define CA_HASH_BY_BYTES(Type)                                                                        \
    static_assert(::std::has_unique_object_representations_v<Type>,                                    \
                  "CA_HASH_BY_BYTES: 这个类型有填充字节或有多种表示的成员（例如 float / double），"    \
                  "按整块字节算哈希会把这些字节也算进去；请改用 CA_REFLECT 生成字段级特化");           \
    namespace cacore::codegen                                                                          \
    {                                                                                                  \
        template<>                                                                                     \
        struct custom_hash_trait<Type>                                                                 \
        {                                                                                              \
            static void hash(Type const& obj, auto& hasher)                                            \
            {                                                                                          \
                hasher.hash_raw(&obj, sizeof(Type));                                                   \
            }                                                                                          \
        };                                                                                             \
    }
