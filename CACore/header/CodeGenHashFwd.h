#pragma once
//
// CodeGenHashFwd.h — 哈希运行时的**声明部分**：只有特化模板与特化检测这两样。
//
// 给"需要一个类型的哈希走哪条分派分支"的文件用（例如旧设施的 `Hasher.h`），
// 它不必把整套哈希 / 序列化运行时（容器、缓冲区、算法与容器头）拖进 include 图。
// 定义与容器规则在 CodeGenHash.h，宿主类型自己的特化写在类型旁边。

#include "CodeGenTraits.h"

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
