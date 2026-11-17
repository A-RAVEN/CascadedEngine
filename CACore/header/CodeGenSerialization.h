#pragma once
//
// CodeGenSerialization.h — CodeGen 自有的序列化运行时。
//
// 它取代旧的序列化 / 反序列化设施在生成物里的角色：
// 生成的序列化代码不再构造旧对象，而是用这里的写对象 / 读对象。
//
// 与旧设施相比，形状上的三处不同，以及它们各自要解决的问题：
//
//   1. 写对象只持有缓冲区、只往后追加，没有位置状态（旧写对象也一样，这里只是把它独立出来）；
//   2. 读对象持有字节起点、长度与已读位置，**位置是调用方持有的变量**（引用传进来的）。
//      旧读对象把偏移私有化且没有取值方法，所以生成代码无法在流的中间调用另一个类型的生成代码
//      ——「容器里装结构体」只能拒绝（旧工具的退出码 12）。位置变成显式参数后，这个形状由递归读写接手。
//   3. 读对象带长度：越过载荷末尾时失败并返回 false，而不是读越界内存；定长数组（`castl::array`
//      这一类没有 `clear()` 的容器）按位置逐个覆写，并在读之前校验载荷长度与 extent 一致。
//
// 字节契约与旧运行时**逐字节一致**，每一条都写死在 Tools/CodeGen/README.md 的「字节契约」一节：
//   标量按自身宽度、本机字节序；字符串先写 uint64 个数、再写字节；容器先写 uint64 个数、再写元素。
// 这条一致性是迁移期的对照手段：同一个对象用新旧两套各写一遍，字节必须相同。

#include "CodeGenTraits.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace cacore::codegen
{
    // ============================================================================================
    // 容器判据（新运行时自有，不看旧头）：有 `size()` 与 `value_type`，或 C 数组按 extent。
    // 与生成的字段表里那个 `FieldFlags::Container` 标志同口径 —— 两边不一致就会一边写容器、
    // 另一边当普通结构体写，所以消费单元里有一条编译期断言把两者钉在一起。
    // ============================================================================================
    template<typename T, typename = void>
    struct isSizedContainer : std::false_type {};

    template<typename T>
    struct isSizedContainer<T, std::void_t<typename T::value_type,
                                           decltype(std::declval<T const&>().size())>> : std::true_type {};

    template<typename T>
    struct isContainer : std::integral_constant<bool, std::is_array_v<T> || isSizedContainer<T>::value> {};

    // 无序容器：有 `bucket_count()`（`unordered_map` / `unordered_set`）。
    template<typename T, typename = void>
    struct isUnorderedContainer : std::false_type {};

    template<typename T>
    struct isUnorderedContainer<T, std::void_t<decltype(std::declval<T const&>().bucket_count())>>
        : std::true_type {};

    // map 类：有 `mapped_type`（`map` / `unordered_map`）。
    template<typename T, typename = void>
    struct isMapLike : std::false_type {};

    template<typename T>
    struct isMapLike<T, std::void_t<typename T::mapped_type>> : std::true_type {};

    // map / set 的 `value_type` 带 const 键，展平排序需要可赋值元素。
    template<typename T, typename = void>
    struct flatElement { using type = T; };

    template<typename T>
    struct flatElement<T, std::void_t<typename T::first_type, typename T::second_type>>
    {
        using type = std::pair<std::remove_const_t<typename T::first_type>, typename T::second_type>;
    };

    // ============================================================================================
    // 包装类型与句柄类型：判据与主模板在 CodeGenTraits.h 里（宿主类型自己给特化，
    // 例如 `uenum::EnumFlags<T>` 写在 uenum.h 里，那里不必把整个运行时拖进去）。
    // ============================================================================================

    // ============================================================================================
    // 字节来源的两种形状，读对象自己按形状分支（不做"选型特化"）：
    //   * 有 `data()` 与 `size()` 的——缓冲区：能拿到长度，读越界会失败；
    //   * 只有 `Read(size, dest)` 的——流式来源（例如 IOManager 的 `IOBatch`）：问不到剩余长度，
    //     每次读交给来源自己判断；来源报告"没接受"时，读对象把这次失败传播出去（与缓冲区分支同一语义）。
    // 判别写在读对象内部而不是让宿主特化：特化要靠每个消费单元 include 到位，漏一个就会静默退回
    // 缓冲区读对象、报一串与真实原因无关的错（实测过），而形状判别在哪里都成立。
    // ============================================================================================
    template<typename Source, typename = void>
    struct hasBufferShape : std::false_type {};

    template<typename Source>
    struct hasBufferShape<Source,
                          std::void_t<decltype(std::declval<Source const&>().data()),
                                      decltype(std::declval<Source const&>().size())>> : std::true_type {};

    template<typename ByteBuffer>
    class writer
    {
    public:
        explicit writer(ByteBuffer& buffer) noexcept : m_Buffer(buffer) {}

        void write_raw(void const* data, std::size_t size)
        {
            const std::size_t endPoint = m_Buffer.size();
            m_Buffer.resize(endPoint + size);
            if (size != 0)
                std::memcpy(m_Buffer.data() + endPoint, data, size);
        }

        // 只按字节写标量：整数 / 浮点 / 布尔 / 枚举。其余类型必须有自己的生成代码，
        // 或者被放进容器 / 包装里 —— 这里刻意不做任何兜底，避免把 padding 或私有布局写进载荷。
        template<typename T>
        void write(T const& value)
        {
            static_assert(std::is_fundamental_v<T> || std::is_enum_v<T>,
                          "CodeGen 写对象只按字节写标量（整数 / 浮点 / 布尔 / 枚举）。"
                          "这个类型既不是标量、也没有生成代码：给它加 CA_REFLECT 标记并生成代码，"
                          "或者把它作为容器元素写。");
            write_raw(&value, sizeof(T));
        }

        ByteBuffer& buffer() noexcept { return m_Buffer; }

    private:
        ByteBuffer& m_Buffer;
    };

    // ============================================================================================
    // 读对象：持有字节来源与已读位置。位置是**引用**，所以它在整条调用链上就是同一个变量：
    // 生成代码可以把自己的位置交给另一个类型的生成代码，读完继续。
    // 来源是缓冲区时走"带长度检查"的分支；是流式来源时走来源自己的 `Read`。
    // ============================================================================================
    template<typename ByteBuffer>
    class reader
    {
    public:
        reader(ByteBuffer const& buffer, std::size_t& position) noexcept
            : m_Buffer(&buffer), m_Position(position)
        {
            if constexpr (hasBufferShape<ByteBuffer>::value)
            {
                if (m_Position > m_Buffer->size())
                {
                    m_Position = m_Buffer->size();
                    m_Failed = true;
                }
            }
        }

        // 缓冲区来源：越界时失败并返回 false，不读长度之外的内存。
        // 流式来源：把来源 `Read` 的返回值传播出来 —— 来源拒绝（越界/来源错误）与缓冲区的越界失败同一语义。
        bool read_raw(void* dest, std::size_t size)
        {
            if constexpr (hasBufferShape<ByteBuffer>::value)
            {
                if (size > m_Buffer->size() - m_Position)
                {
                    m_Failed = true;
                    return false;
                }
                if (size != 0)
                    std::memcpy(dest, m_Buffer->data() + m_Position, size);
                m_Position += size;
                return true;
            }
            else
            {
                // `Read` 是来源的成员函数（推进它自己的读游标），生成代码拿到的却是 `const&`。
                // 来源报告"这次读没被接受"时置失败位并返回 false：旧实现无条件 `return true`，
                // 于是截短/损坏的载荷被当成读成功、上层拿到垃圾数据（design D3）。
                if (!const_cast<ByteBuffer*>(m_Buffer)->Read(size, dest))
                {
                    m_Failed = true;
                    return false;
                }
                m_Position += size;
                return true;
            }
        }

        template<typename T>
        bool read(T& value)
        {
            static_assert(std::is_fundamental_v<T> || std::is_enum_v<T>,
                          "CodeGen 读对象只按字节读标量（整数 / 浮点 / 布尔 / 枚举）。"
                          "这个类型既不是标量、也没有生成代码：给它加 CA_REFLECT 标记并生成代码，"
                          "或者把它作为容器元素读。");
            return read_raw(&value, sizeof(T));
        }

        ByteBuffer const& buffer() const noexcept { return *m_Buffer; }

        // 位置本身（引用），供递归调用把「读到哪里了」交给另一个类型的生成代码。
        std::size_t& position() noexcept { return m_Position; }

        std::size_t offset() const noexcept { return m_Position; }
        std::size_t size() const noexcept
        {
            if constexpr (hasBufferShape<ByteBuffer>::value) return m_Buffer->size();
            else return 0;   // 流式来源问不到长度
        }

        bool ok() const noexcept { return !m_Failed; }
        void fail() noexcept { m_Failed = true; }

        // 定长数组专用：载荷里的长度与目标 extent 不一致时失败，并把两个长度都留下，
        // 让调用方能报出"读到的是 4、目标只有 3"这种话，而不是只说一句"失败了"。
        void fail_length(std::size_t extent, std::size_t payloadLength) noexcept
        {
            m_Failed = true;
            m_LengthMismatch = true;
            m_Extent = extent;
            m_PayloadLength = payloadLength;
        }

        bool lengthMismatch(std::size_t& extent, std::size_t& payloadLength) const noexcept
        {
            extent = m_Extent;
            payloadLength = m_PayloadLength;
            return m_LengthMismatch;
        }

    private:
        ByteBuffer const* m_Buffer;
        std::size_t&      m_Position;
        bool              m_Failed = false;
        bool              m_LengthMismatch = false;
        std::size_t       m_Extent = 0;
        std::size_t       m_PayloadLength = 0;
    };

    // 生成代码里的读写入口按这个别名取读对象；缓冲区与流式来源都是同一个 `reader`，
    // 形状分支在读对象内部（`hasBufferShape`）—— 生成物不需要为来源形状分叉。
    template<typename Source>
    using reader_t = reader<Source>;

    // ============================================================================================
    // 生成物的读写入口：序列化发射器为每个已标注类型生成一个特化。
    // 主模板刻意不带 `serialize` / `deserialize` 成员 —— 这样 `hasGeneratedSerializer` 的探测得到
    // false，而不是落在一个会静默丢数据的兜底实现上。
    // ============================================================================================
    template<typename T>
    struct CodeGenSerializer {};

    template<typename T, typename ByteBuffer, typename = void>
    struct hasGeneratedSerializer : std::false_type {};

    template<typename T, typename ByteBuffer>
    struct hasGeneratedSerializer<T, ByteBuffer, std::void_t<
        decltype(CodeGenSerializer<T>::serialize(std::declval<T const&>(), std::declval<ByteBuffer&>())),
        decltype(CodeGenSerializer<T>::deserialize(std::declval<T&>(), std::declval<ByteBuffer const&>(),
                                                   std::declval<std::size_t&>()))>> : std::true_type {};

    // ============================================================================================
    // 递归读写：容器是结构递归（`vector<vector<int>>` / `map<K, set<V>>` / `T m[2][3]` 都覆盖），
    // 元素是另一个已标注类型时，交给它自己的生成代码，位置原样传下去。
    // ============================================================================================
    template<typename ByteBuffer, typename C> void cgWriteContainer(writer<ByteBuffer>& w, C const& c);
    template<typename Reader, typename C> bool cgReadContainer(Reader& r, C& c);

    // 容器的读取路径不假定容器一定有 `clear()`：
    //   * 有 `clear()` 的先清空（无序容器按载荷重建，结果只取决于载荷）；
    //   * 定长数组（`std::array` / `castl::array`，只有下标与 `size()`）按位置逐个覆写，
    //     并在读之前校验载荷里的长度与 extent 一致 —— 不一致就是格式错，直接失败。
    template<typename C, typename = void> struct hasClear : std::false_type {};
    template<typename C> struct hasClear<C, std::void_t<decltype(std::declval<C&>().clear())>> : std::true_type {};

    template<typename C, typename = void> struct hasReserve : std::false_type {};
    template<typename C> struct hasReserve<C, std::void_t<decltype(std::declval<C&>().reserve(std::size_t{}))>> : std::true_type {};

    template<typename C, typename = void> struct hasPushBack : std::false_type {};
    template<typename C> struct hasPushBack<C, std::void_t<decltype(std::declval<C&>().push_back(std::declval<typename C::value_type>()))>> : std::true_type {};

    template<typename C, typename = void> struct hasIndexer : std::false_type {};
    template<typename C> struct hasIndexer<C, std::void_t<decltype(std::declval<C&>()[std::size_t{}])>> : std::true_type {};

    // `set` / `unordered_set`：只能插入，没有下标也没有 push_back。
    template<typename C, typename = void> struct hasInsertAtEnd : std::false_type {};
    template<typename C> struct hasInsertAtEnd<C, std::void_t<decltype(std::declval<C&>().insert(std::declval<C&>().end(), std::declval<typename C::value_type>()))>> : std::true_type {};

    template<typename ByteBuffer, typename E>
    void cgWriteValue(writer<ByteBuffer>& w, E const& e)
    {
        using V = std::remove_cvref_t<E>;
        if constexpr (std::is_array_v<V>) cgWriteContainer(w, e);
        else if constexpr (isContainer<V>::value) cgWriteContainer(w, e);
        else if constexpr (wrapper_traits<V>::is_wrapper) cgWriteValue(w, wrapper_traits<V>::get_data(e));
        else if constexpr (!isMapLike<V>::value && requires { e.first; e.second; })
        {
            // map / set 的元素是 pair：两个值各写一遍，与旧运行时的逐成员写法一致。
            cgWriteValue(w, e.first);
            cgWriteValue(w, e.second);
        }
        else if constexpr (hasGeneratedSerializer<V, ByteBuffer>::value)
            CodeGenSerializer<V>::serialize(e, w.buffer());
        else
            w.write(e);
    }

    // 写：元素个数，然后是元素。遍历顺序不确定的容器先排成规范顺序，使字节只取决于内容。
    template<typename ByteBuffer, typename C>
    void cgWriteContainerSized(writer<ByteBuffer>& w, C const& c)
    {
        using E0 = typename C::value_type;
        using E = typename flatElement<E0>::type;   // map / set 的 value_type 带 const 键
        if constexpr (isUnorderedContainer<C>::value)
        {
            std::vector<E> flat(c.begin(), c.end());
            std::sort(flat.begin(), flat.end(), [](E const& a, E const& b) {
                if constexpr (isMapLike<C>::value) return a.first < b.first;
                else return a < b; });
            w.write(static_cast<unsigned long long>(flat.size()));
            for (auto const& e : flat) cgWriteValue(w, e);
        }
        else
        {
            w.write(static_cast<unsigned long long>(c.size()));
            for (auto const& e : c) cgWriteValue(w, e);
        }
    }

    // C 数组（含每一维）写成（extent，元素），与旧运行时的多维形状一致。
    template<typename ByteBuffer, typename C>
    void cgWriteContainer(writer<ByteBuffer>& w, C const& c)
    {
        if constexpr (std::is_array_v<C>)
        {
            w.write(static_cast<unsigned long long>(std::extent_v<C>));
            for (auto const& e : c) cgWriteValue(w, e);
        }
        else cgWriteContainerSized(w, c);
    }

    // 读的那几个入口对"读对象"是泛型的：缓冲区与流式来源都用同一个读对象（形状分支在它内部）。
    template<typename Reader, typename C>
    bool cgReadContainerSized(Reader& r, C& c)
    {
        unsigned long long n = 0;
        if (!r.read(n)) return false;
        if constexpr (hasClear<C>::value) c.clear();
        if constexpr (hasReserve<C>::value) c.reserve(static_cast<std::size_t>(n));

        if constexpr (hasPushBack<C>::value)
        {
            for (unsigned long long i = 0; i < n; ++i)
            {
                typename C::value_type e{};
                if (!cgReadValue(r, e)) return false;
                c.push_back(std::move(e));
            }
            return true;
        }
        else if constexpr (isMapLike<C>::value)
        {
            using E = typename C::value_type;
            using K = std::remove_const_t<typename E::first_type>;
            using V = typename E::second_type;
            for (unsigned long long i = 0; i < n; ++i)
            {
                K k{}; V v{};
                if (!cgReadValue(r, k)) return false;
                if (!cgReadValue(r, v)) return false;
                c.insert(std::make_pair(std::move(k), std::move(v)));
            }
            return true;
        }
        else if constexpr (hasInsertAtEnd<C>::value)
        {
            for (unsigned long long i = 0; i < n; ++i)
            {
                typename C::value_type e{};
                if (!cgReadValue(r, e)) return false;
                c.insert(c.end(), std::move(e));
            }
            return true;
        }
        else if constexpr (hasIndexer<C>::value)
        {
            // 定长数组：没有 clear()，也不能插入 —— 先校验长度，再按位置逐个覆写。
            if (n != static_cast<unsigned long long>(c.size()))
            {
                r.fail_length(c.size(), static_cast<std::size_t>(n));
                return false;
            }
            for (unsigned long long i = 0; i < n; ++i)
                if (!cgReadValue(r, c[i])) return false;
            return true;
        }
        else
        {
            static_assert(sizeof(C) == 0,
                          "这个容器既没有 push_back()、也不是 map 类、也没有下标：CodeGen 读路径不认识它。"
                          "给它加一个下标、或者换成 vector / map / set / 定长数组。");
            return false;
        }
    }

    template<typename Reader, typename C>
    bool cgReadContainer(Reader& r, C& c)
    {
        if constexpr (std::is_array_v<C>)
        {
            unsigned long long n = 0;
            if (!r.read(n)) return false;
            if (n != static_cast<unsigned long long>(std::extent_v<C>))
            {
                r.fail_length(static_cast<std::size_t>(std::extent_v<C>), static_cast<std::size_t>(n));
                return false;
            }
            for (unsigned long long i = 0; i < n; ++i)
                if (!cgReadValue(r, c[i])) return false;
            return true;
        }
        else return cgReadContainerSized(r, c);
    }

    template<typename Reader, typename E>
    bool cgReadValue(Reader& r, E& e)
    {
        using V = std::remove_cvref_t<E>;
        if constexpr (std::is_array_v<V>) return cgReadContainer(r, e);
        else if constexpr (isContainer<V>::value) return cgReadContainer(r, e);
        else if constexpr (wrapper_traits<V>::is_wrapper)
        {
            typename wrapper_traits<V>::inner_type inner{};
            if (!cgReadValue(r, inner)) return false;
            wrapper_traits<V>::set_data(e, inner);
            return true;
        }
        else if constexpr (!isMapLike<V>::value && requires { e.first; e.second; })
        {
            return cgReadValue(r, e.first) && cgReadValue(r, e.second);
        }
        // 读对象的来源类型：缓冲区读对象是那个缓冲区，流式读对象是那个流。
        else if constexpr (hasGeneratedSerializer<V, std::remove_cvref_t<decltype(r.buffer())>>::value)
        {
            // 把位置本身交出去：另一个类型的生成代码接着往下读。
            return CodeGenSerializer<V>::deserialize(e, r.buffer(), r.position());
        }
        else return r.read(e);
    }

    // 独立入口（不经过另一个类型）：从头读，位置在函数内部持有。
    template<typename T, typename ByteBuffer>
    bool readStruct(T& obj, ByteBuffer const& buffer)
    {
        std::size_t position = 0;
        return CodeGenSerializer<T>::deserialize(obj, buffer, position);
    }

} // namespace cacore::codegen
