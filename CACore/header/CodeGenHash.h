#pragma once
//
// CodeGenHash.h — CodeGen 自有的哈希运行时。
//
// 它取代旧哈希设施（那套"把对象解包成成员再逐个算"的运行时，随旧头一起退场）在生成物里的角色。
// 生成物提供的特化是 `cacore::codegen::custom_hash_trait<T>`，驱动它的是这里的哈希对象，两者都在这个
// 头里，不再引用旧头里的任何符号。
//
// 三条规则，与生成的哈希发射器同口径：
//
//   1. **按字段算，不按整块字节**：拿到一个对象的哈希，走的是它自己的特化（逐个成员），
//      所以 padding 字节不同不影响哈希，`-0.0` 与 `+0.0` 一样，NaN 有稳定的规范值。
//   2. **分派顺序：自定义特化优先于「平凡可拷贝」**。旧设施把「平凡可拷贝」放在第一位，
//      纯 POD 类型永远轮不到自己的特化（不报错，静默不生效）。这里反过来，
//      所以「这个类型有生成的特化」就一定走它。
//   3. **容器先排规范顺序再算**：遍历顺序不确定的容器（`unordered_map` / `unordered_set`）
//      先按内容排序，元素显式调用自身的特化，所以哈希只取决于内容，与插入历史无关。

#include "CodeGenSerialization.h"
#include "CodeGenHashFwd.h"

#include <CACore/CAHash.h>
#include <CASTL/CAContainerBase.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace cacore::codegen
{
    using default_hash_algorithm = cahash::constexpr_fnvla_64_Hash;

    // 生成物给出的相等特化模板：与哈希同源，避免出现「哈希按字段、比较按字节」的错配。
    template<typename T>
    struct CodeGenEqual {};

    template<typename T, typename = void>
    struct hasGeneratedEqual : std::false_type {};

    template<typename T>
    struct hasGeneratedEqual<T, std::void_t<decltype(CodeGenEqual<T>::equal(std::declval<T const&>(),
                                                                           std::declval<T const&>()))>>
        : std::true_type {};

    template<typename T>
    concept has_std_hash = requires(T t)
    {
        castl::hash<T>{}(t);
    };

    // 容器相等：C 数组按元素递归（`std::equal` 会把多维数组的行按地址比），
    // 有序容器逐元素比，元素走 `cgEqualValue`（可以与生成的相等特化同源），
    // map 类与无序容器按容器自己的 `==` 比（它们本来就是按内容定义的）。
    template<typename T> bool cgEqualValue(T const& a, T const& b);
    template<typename C> bool cgEqualContainer(C const& a, C const& b);

    template<typename T>
    bool cgEqualValue(T const& a, T const& b)
    {
        using V = std::remove_cv_t<T>;
        if constexpr (std::is_array_v<V>) return cgEqualContainer(a, b);
        else if constexpr (isContainer<V>::value) return cgEqualContainer(a, b);
        else if constexpr (wrapper_traits<V>::is_wrapper)
            return cgEqualValue(wrapper_traits<V>::get_data(a), wrapper_traits<V>::get_data(b));
        else if constexpr (!isMapLike<V>::value && requires { a.first; a.second; })
            return cgEqualValue(a.first, b.first) && cgEqualValue(a.second, b.second);
        else if constexpr (hasGeneratedEqual<V>::value) return CodeGenEqual<V>::equal(a, b);
        else return a == b;
    }

    // 容器相等：**不用容器自己的 `operator==`**，而是按元素逐个比（元素走 `cgEqualValue`）。
    // 理由是"同源"：容器的相等口径与哈希口径必须是同一套 —— 哈希那边按规范顺序遍历元素、元素用自己的
    // 特化；相等这边如果换成 `a == b`，就会要求元素有 `operator==`，而生成代码覆盖的元素类型可以有
    // 生成的 `equal` 却没有 `operator==`（实测：`unordered_map<NameHash, ShaderStructData>` 会因此编不过）。
    // 遍历顺序不确定的容器先排成与哈希相同的规范顺序，再逐个比。
    template<typename C>
    bool cgEqualContainer(C const& a, C const& b)
    {
        using V = std::remove_cv_t<C>;
        if constexpr (std::is_array_v<V>)
        {
            for (std::size_t i = 0; i < std::extent_v<V>; ++i)
                if (!cgEqualContainer(a[i], b[i])) return false;
            return true;
        }
        else if constexpr (isContainer<V>::value)
        {
            if (a.size() != b.size()) return false;
            if constexpr (isUnorderedContainer<V>::value)
            {
                using E0 = typename V::value_type;
                using E = typename flatElement<E0>::type;
                std::vector<E> fa(a.begin(), a.end());
                std::vector<E> fb(b.begin(), b.end());
                const auto less = [](E const& x, E const& y) {
                    if constexpr (isMapLike<V>::value) return x.first < y.first;
                    else return x < y;
                };
                std::sort(fa.begin(), fa.end(), less);
                std::sort(fb.begin(), fb.end(), less);
                for (std::size_t i = 0; i < fa.size(); ++i)
                    if (!cgEqualValue(fa[i], fb[i])) return false;
                return true;
            }
            else
            {
                auto ia = a.begin();
                auto ib = b.begin();
                for (; ia != a.end(); ++ia, ++ib)
                    if (!cgEqualValue(*ia, *ib)) return false;
                return true;
            }
        }
        else return a == b;
    }

    // ============================================================================================
    // 哈希对象：与旧设施同形（`hash_raw` / `hash(值)` / `getHash`），宿主换成新头。
    // ============================================================================================
    template<typename hashAlg = default_hash_algorithm>
    class hasher
    {
    public:
        using result_type = typename hashAlg::result_type;

        hashAlg alg = {};

        result_type getHash() const noexcept { return static_cast<result_type>(alg); }
        result_type getHash() noexcept { return static_cast<result_type>(alg); }

        void hash_raw(void const* data, std::size_t size) noexcept { alg(data, size); }

        template<typename Obj>
        void hash(Obj const& object)
        {
            using objType = std::remove_cvref_t<Obj>;

            // 1. 自定义特化优先。生成物提供的特化必须在这里被选中，否则「迁移完成、哈希没生效」。
            if constexpr (has_custom_hash_func<objType, hasher>)
            {
                custom_hash_trait<objType>::hash(object, *this);
            }
            // 2. 句柄：按它持有的指针地址算。
            else if constexpr (pointer_traits<objType>::is_pointer_handle)
            {
                const std::uint64_t address = reinterpret_cast<std::uint64_t>(pointer_traits<objType>::get_pointer(object));
                hash_raw(&address, sizeof(address));
            }
            else if constexpr (std::is_pointer_v<objType>)
            {
                const std::uint64_t address = reinterpret_cast<std::uint64_t>(object);
                hash_raw(&address, sizeof(address));
            }
            else if constexpr (std::is_fundamental_v<objType> || std::is_enum_v<objType>)
            {
                hash_raw(&object, sizeof(objType));
            }
            // 3. 容器：先排规范顺序。
            else if constexpr (isContainer<objType>::value)
            {
                cgHashContainer(*this, object);
            }
            // 4. 包装类型：算它包着的东西。
            else if constexpr (wrapper_traits<objType>::is_wrapper)
            {
                hash(wrapper_traits<objType>::get_data(object));
            }
            else
            {
                // 走到这里说明这个类型**没有可用的来源**：没有生成/手写特化、不是容器 / 包装 / 标量 / 指针，
                // 也没有按字节的显式声明。以前这里会"平凡可拷贝就悄悄按整块字节"，于是"某个编译单元看不见特化"
                // 与"这个类型本来就该按字节"长得一模一样，链接期合并弱符号之后整个二进制的口径被换掉而没有
                // 任何诊断。现在必须是编译错误，而且要说清三条出路。
                static_assert(sizeof(objType) == 0,
                              "这个类型没有可用的哈希来源：没有生成代码、没有手写的 custom_hash_trait 特化，"
                              "也不是容器 / 包装 / 标量 / 指针。三条出路：① 给它加 CA_REFLECT 标记并生成字段级特化；"
                              "② 给它写一个 custom_hash_trait 特化；③ 它确实该按整块字节算（没有填充字节、"
                              "也没有 float/double 成员）时，用 CA_HASH_BY_BYTES(类型) 显式声明它。");
            }
        }
    };

    // ============================================================================================
    // 容器哈希：遍历顺序不确定的先排成规范顺序；每个元素显式调用自身的特化（`h.hash(元素)`
    // 对平凡可拷贝的元素会走整块字节那条捷径，永远到不了元素自己的特化）；浮点先规范化。
    // ============================================================================================
    template<typename H, typename C> void cgHashContainer(H& h, C const& c);

    template<typename H, typename E>
    void cgHashContainerValue(H& h, E const& e)
    {
        using V = std::remove_cvref_t<E>;
        if constexpr (std::is_same_v<V, float>)
        {
            float v = e;
            if (v == 0.0f) v = 0.0f;
            else if (v != v) v = std::numeric_limits<float>::quiet_NaN();
            h.hash_raw(&v, sizeof(v));
        }
        else if constexpr (std::is_same_v<V, double>)
        {
            double v = e;
            if (v == 0.0) v = 0.0;
            else if (v != v) v = std::numeric_limits<double>::quiet_NaN();
            h.hash_raw(&v, sizeof(v));
        }
        else if constexpr (std::is_array_v<V>) cgHashContainer(h, e);
        else if constexpr (isContainer<V>::value) cgHashContainer(h, e);
        else if constexpr (has_custom_hash_func<V, H>) custom_hash_trait<V>::hash(e, h);
        else h.hash(e);
    }

    template<typename H, typename C>
    void cgHashContainerSized(H& h, C const& c)
    {
        using E0 = typename C::value_type;
        using E = typename flatElement<E0>::type;
        if constexpr (isUnorderedContainer<C>::value)
        {
            std::vector<E> flat(c.begin(), c.end());
            std::sort(flat.begin(), flat.end(), [](E const& a, E const& b) {
                if constexpr (isMapLike<C>::value) return a.first < b.first;
                else return a < b; });
            h.hash(static_cast<unsigned long long>(flat.size()));
            for (auto const& e : flat)
            {
                if constexpr (isMapLike<C>::value)
                {
                    cgHashContainerValue(h, e.first);
                    cgHashContainerValue(h, e.second);
                }
                else cgHashContainerValue(h, e);
            }
        }
        else
        {
            h.hash(static_cast<unsigned long long>(c.size()));
            for (auto const& e : c)
            {
                if constexpr (isMapLike<C>::value)
                {
                    cgHashContainerValue(h, e.first);
                    cgHashContainerValue(h, e.second);
                }
                else cgHashContainerValue(h, e);
            }
        }
    }

    template<typename H, typename C>
    void cgHashContainer(H& h, C const& c)
    {
        if constexpr (std::is_array_v<C>)
        {
            h.hash(static_cast<unsigned long long>(std::extent_v<C>));
            for (auto const& e : c) cgHashContainerValue(h, e);
        }
        else cgHashContainerSized(h, c);
    }

} // namespace cacore::codegen
