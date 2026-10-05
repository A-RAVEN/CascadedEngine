#pragma once
//
// HashKeys.h —— 宿主哈希类型：`NameHash` / `PathHash` / `HashObj` 与它们要用的那点东西。
//
// 这些类型是**保留**的（引擎到处在用它们当字典键），与"随旧头一起删掉"的旧哈希驱动分开住：
// `cacore::hash<T>` 的取值现在由 `cacore::codegen::hasher` 承担（见下面 `hash` 的注释），旧驱动
// （`aggregateHasher` 那套）留在 `Hasher.h` 里等 9.3 删掉。本头**不依赖**那三份待删的头。
#include <CACore/CAHash.h>
#include <CASTL/CAFileSystem.h>
#include <CASTL/CAString.h>
#include <CACore/CALiteralString.h>
#include "CodeGenHashFwd.h"  // 先来这份轻的：它只带 `CodeGenTraits.h`（`wrapper_traits` / `pointer_traits`
                            // 的主模板）。顺序重要 —— 本头会被 `CodeGenHash.h` 那条链反过来包含，
                            // 主模板没先声明的话，下面那几份特化会报 "undeclared template struct"。
#include "CodeGenHash.h"    // `hash` / `hashable` 的取值用 codegen::hasher 本体，不只是一句声明

#include <type_traits>
#include <cstddef>
#include <functional>

namespace cacore
{

    using default_hashclass = typename cahash::constexpr_fnvla_64_Hash;

    template<typename T>
    concept has_std_hash = requires(T t)
    {
        castl::hash<T>{}(t);
    };

    // `cacore::hash<T, Alg>` 的定义在本文件下面（`hash` 那个结构）：非标注类型要靠它走老反射算哈希。
    // 这里留一句前置声明只是为了下面三个键类型能先用名字。
    template<typename T, typename hashAlg>
    struct hash;

    template<typename T>
	using hash_256 = hash<T, cahash::sha256_hash>;

    enum class EHashObjCompareMode
    {
        HashOnly,
		FullCompare,
        SHA256
    };

    // 把两个哈希值揉在一起的小工具（Vulkan 的 `ShaderLibrary` 拿它拼自己那个 `GetHash()`）。
    // 它原来住在 `Hasher.h` 里，与"要删的旧驱动"无关，所以随宿主类型一起搬到本头。
    template<typename T>
    inline size_t hash_combine(size_t seed, T const& val)
    {
        seed ^= std::hash<T>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }


    template<typename ObjType, EHashObjCompareMode CompareMode = EHashObjCompareMode::SHA256, typename hashAlg = default_hashclass>
    struct HashObj
    {
    public:
        using result_type = hashAlg::result_type;
        using obj_type = ObjType;
        HashObj() = default;
        HashObj(ObjType const& obj) : m_Object(obj)
        {
			UpdateHash();
		}
        HashObj(HashObj const& hashObj) : m_Object(hashObj.m_Object)
            , m_HashValue(hashObj.m_HashValue)
            , m_HashValid(hashObj.m_HashValid)
        {
            if constexpr (CompareMode == EHashObjCompareMode::SHA256)
            {
                m_SHA256HashValue = hash<ObjType, cahash::sha256_hash>{}(m_Object);
            }
        }
        constexpr HashObj& operator=(HashObj const& other)
        {
            m_Object = other.m_Object;
            m_HashValue = other.m_HashValue;
            m_HashValid = other.m_HashValid;
            if constexpr (CompareMode == EHashObjCompareMode::SHA256)
            {
                m_SHA256HashValue = hash<ObjType, cahash::sha256_hash>{}(m_Object);
            }
            return *this;
        }
        constexpr ObjType const& Get() const noexcept
		{
			return m_Object;
		}
        ObjType const* operator->() const noexcept
        {
            return &m_Object;
        }
        constexpr result_type GetHash() const noexcept
		{
			return m_HashValue;
		}

        constexpr bool Valid() const noexcept
        {
            return m_HashValid;
        }

        constexpr void Reset() noexcept
        {
            m_HashValid = false;
        }

        constexpr auto operator<=>(HashObj const& b) const
        {
			if constexpr (CompareMode == EHashObjCompareMode::SHA256)
			{
				return m_SHA256HashValue <=> b.m_SHA256HashValue;
			}
			else if constexpr (CompareMode == EHashObjCompareMode::FullCompare)
			{
				return m_Object <=> b.m_Object;
			}
			else
			{
				return m_HashValue <=> b.m_HashValue;
			}
        }

        constexpr bool operator==(HashObj const& b) const
        {
            return m_HashValue == b.m_HashValue;
        }

		static HashObj const& SelectIfValid(HashObj const& inObj, HashObj const& fallback)
		{
			if (inObj.Valid())
			{
				return inObj;
			}
			return fallback;
		}

    private:
        ObjType m_Object{};
        result_type m_HashValue{};
		cahash::sha256_hash::result_type m_SHA256HashValue{};
        bool m_HashValid = false;
        void UpdateHash()
        {
            m_HashValue = hash<ObjType, hashAlg>{}(m_Object);
            if constexpr (CompareMode == EHashObjCompareMode::SHA256)
            {
                m_SHA256HashValue = hash<ObjType, cahash::sha256_hash>{}(m_Object);
            }
            m_HashValid = true;
		}

    };

    struct PathHash
    {
    public:
        using result_type = default_hashclass::result_type;
        using obj_type = castl::string;

        constexpr PathHash() : m_HashValue(0), m_HashValid(false), m_Name("") {}
        PathHash(const char* str)
        {
            if (str == nullptr)
            {
                Reset();
                return;
            }
            m_Name = cafs::path(str).generic_string();
            m_NameView = m_Name;
            UpdateHash();
        }
        PathHash(castl::string const& str) : m_Name(cafs::path(str).generic_string())
        {
            m_NameView = m_Name;
            UpdateHash();
        }
        PathHash(cafs::path const& path) : m_Name(path.generic_string())
        {
            m_NameView = m_Name;
            UpdateHash();
        }

        constexpr PathHash(PathHash const& pathHash) : m_Name(pathHash.m_Name)
            , m_HashValue(pathHash.m_HashValue)
            , m_HashValid(pathHash.m_HashValid)
        {
            if (m_Name.empty())
            {
                m_NameView = pathHash.m_NameView;
            }
            else
            {
                m_NameView = m_Name;
            }
        }
        constexpr PathHash& operator=(PathHash const& pathHash)
        {
            m_Name = pathHash.m_Name;
            m_HashValue = pathHash.m_HashValue;
            m_HashValid = pathHash.m_HashValid;
            if (m_Name.empty())
            {
                m_NameView = pathHash.m_NameView;
            }
            else
            {
                m_NameView = m_Name;
            }
            return *this;
        }

        constexpr castl::string string() const noexcept
        {
            return castl::string(m_NameView);
        }

        constexpr char const* c_str() const noexcept
        {
            return m_NameView.data();
        }

        constexpr operator char const* () const noexcept
        {
            return m_NameView.data();
        }

        constexpr operator castl::string_view const& () const noexcept
        {
            return m_NameView;
        }

        constexpr operator castl::string() const noexcept
        {
            return string();
        }

        constexpr castl::string_view const& Get() const noexcept
        {
            return m_NameView;
        }

        constexpr castl::string_view const* operator->() const noexcept
        {
            return &m_NameView;
        }

        constexpr result_type GetHash() const noexcept
        {
            return m_HashValue;
        }

        constexpr bool Valid() const noexcept
        {
            return m_HashValid;
        }

        constexpr void Reset() noexcept
        {
            m_HashValid = false;
        }

        constexpr auto operator<=>(PathHash const& b) const
        {
            return m_NameView <=> b.m_NameView;
        }

        bool operator==(PathHash const& b) const
        {
            return m_NameView == b.m_NameView;
        };

        void UpdateHash()
        {
            default_hashclass hasher{};
            hasher(m_Name.data(), m_Name.size());
            m_HashValue = static_cast<result_type>(hasher);
            m_HashValid = true;
        }

    private:
        constexpr PathHash(const char* str, result_type hashVal) : m_Name()
            , m_NameView(str)
            , m_HashValue(hashVal)
            , m_HashValid(true)
        {
        }
    public:

        template <char... c>
        static PathHash const& StaticPathHash() {
            constexpr static std::size_t n = sizeof...(c);
            constexpr static const char data[n] = { c... };
            default_hashclass hasher{};
            hasher(data, n);
            static const result_type hashVal = static_cast<result_type>(hasher);
            static const PathHash pathHash(data, hashVal);
            return pathHash;
        };

        template <castl::string_literal str, size_t... N>
        static PathHash const& StaticPathHashInternal(castl::index_sequence<N...>) {
            return StaticPathHash<str.template get_char<N>()...>();
        }

        template <castl::string_literal str>
        static PathHash const& Static() {
            return StaticPathHashInternal<str>(std::make_index_sequence<str.count>{});
        }

    private:
        castl::string_view m_NameView;
        bool m_HashValid;
        result_type m_HashValue;
        castl::string m_Name;
    };

    struct NameHash
    {
    public:
        using result_type = default_hashclass::result_type;
        using obj_type = castl::string;

        constexpr NameHash() : m_HashValue(0), m_HashValid(false), m_Name("") {}
         NameHash(const char* str)
        {
            if (str == nullptr)
            {
                 Reset();
                 return;
            }
			m_Name = str;
            m_NameView = m_Name;
            UpdateHash();
        }
         NameHash(castl::string const& str) : m_Name(str)
        {
			m_NameView = m_Name;
            UpdateHash();
        }
        constexpr NameHash(NameHash const& nameHash) : m_Name(nameHash.m_Name)
            , m_HashValue(nameHash.m_HashValue)
            , m_HashValid(nameHash.m_HashValid)
        {
            if (m_Name.empty())
            {
                m_NameView = nameHash.m_NameView;
            }
            else
            {
				m_NameView = m_Name;
            }
        }
        constexpr NameHash& operator=(NameHash const& nameHash)
        {
            m_Name = nameHash.m_Name;
            m_HashValue = nameHash.m_HashValue;
            m_HashValid = nameHash.m_HashValid;
            if (m_Name.empty())
            {
                m_NameView = nameHash.m_NameView;
            }
            else
            {
                m_NameView = m_Name;
            }
            return *this;
        }

		constexpr castl::string string() const noexcept
		{
			return castl::string(m_NameView);
		}

		constexpr char const* c_str() const noexcept
		{
			return m_NameView.data();
		}

		constexpr operator char const* () const noexcept
		{
			return m_NameView.data();
		}

        constexpr operator castl::string_view const&() const noexcept
        {
            return m_NameView;
        }

        constexpr castl::string_view const& Get() const noexcept
        {
            return m_NameView;
        }

		constexpr castl::string_view const* operator->() const noexcept
		{
			return &m_NameView;
		}

        constexpr result_type GetHash() const noexcept
        {
            return m_HashValue;
        }

        constexpr bool Valid() const noexcept
        {
            return m_HashValid;
        }

        constexpr void Reset() noexcept
        {
            m_HashValid = false;
        }

        constexpr auto operator<=>(NameHash const& b) const
        {
            return m_NameView <=> b.m_NameView;
        }

        bool operator==(NameHash const& b) const
        {
            return m_NameView == b.m_NameView;
        };

        void UpdateHash()
        {
            default_hashclass hasher{};
            hasher(m_Name.data(), m_Name.size());
            m_HashValue = static_cast<result_type>(hasher);
            m_HashValid = true;
        }

    private:
        constexpr NameHash(const char* str, result_type hashVal) : m_Name()
			, m_NameView(str)
            , m_HashValue(hashVal)
            , m_HashValid(true)
        {
        }
    public:

        template <char... c>
        static NameHash const& StaticNameHash() {
            constexpr static std::size_t n = sizeof...(c);
            constexpr static const char data[n] = { c... };
            default_hashclass hasher{};
			hasher(data, n);
            static const result_type hashVal = static_cast<result_type>(hasher);
			static const NameHash nameHash(data, hashVal);
            return nameHash;
        };

        template <castl::string_literal str, size_t... N>
        static NameHash const& StaticNameHashInternal(castl::index_sequence<N...>) {
            return StaticNameHash<str.template get_char<N>()...>();
        }

        template <castl::string_literal str>
        static NameHash const& Static() {
            return StaticNameHashInternal<str>(std::make_index_sequence<str.count>{});
        }

    private:
        castl::string_view m_NameView;
        bool m_HashValid;
        result_type m_HashValue;
        castl::string m_Name;
    };

} // namespace cacore

namespace cacore::codegen
{
    template<>
    struct wrapper_traits<::cacore::PathHash>
    {
        static constexpr bool is_wrapper = true;
        using inner_type = castl::string;
        static inner_type get_data(::cacore::PathHash const& obj) { return obj.string(); }
        static void set_data(::cacore::PathHash& obj, inner_type const& data) { obj = ::cacore::PathHash(data); }
    };

    template<>
    struct wrapper_traits<::cacore::NameHash>
    {
        static constexpr bool is_wrapper = true;
        using inner_type = castl::string;
        static inner_type get_data(::cacore::NameHash const& obj) { return obj.string(); }
        static void set_data(::cacore::NameHash& obj, inner_type const& data) { obj = ::cacore::NameHash{ data }; }
    };

    template<typename ObjType, ::cacore::EHashObjCompareMode CompareMode, typename hashAlg>
    struct wrapper_traits<::cacore::HashObj<ObjType, CompareMode, hashAlg>>
    {
        static constexpr bool is_wrapper = true;
        using inner_type = ObjType;
        static inner_type const& get_data(::cacore::HashObj<ObjType, CompareMode, hashAlg> const& obj)
        {
            return obj.Get();
        }

        static void set_data(::cacore::HashObj<ObjType, CompareMode, hashAlg>& obj, inner_type const& data)
        {
            obj = ::cacore::HashObj<ObjType, CompareMode, hashAlg>{ data };
        }
    };

    template<>
    struct custom_hash_trait<::cacore::PathHash>
    {
        static void hash(::cacore::PathHash const& obj, auto& hasher)
        {
            hasher.hash_raw(obj.Get().data(), obj.Get().size());
        }
    };

    template<>
    struct custom_hash_trait<::cacore::NameHash>
    {
        static void hash(::cacore::NameHash const& obj, auto& hasher)
        {
            hasher.hash_raw(obj.Get().data(), obj.Get().size());
        }
    };

    template<typename ObjType, ::cacore::EHashObjCompareMode CompareMode, typename hashAlg>
    struct custom_hash_trait<::cacore::HashObj<ObjType, CompareMode, hashAlg>>
    {
        static void hash(::cacore::HashObj<ObjType, CompareMode, hashAlg> const& obj, auto& hasher)
        {
            hasher.hash(obj.Get());
        }
    };
}

namespace cacore
{
    template<typename T, typename hashAlg = default_hashclass>
    concept hashable = requires (T const& obj)
    {
        codegen::hasher<hashAlg>{}.hash(obj);
    } || has_std_hash<T>;

    template<typename T, typename hashAlg = default_hashclass>
    struct hash
    {
        using result_type = hashAlg::result_type;
        constexpr result_type operator()(const T& obj) const noexcept
        {
            if constexpr (has_std_hash<T> && std::is_same_v<result_type, size_t>)
            {
                //使用标准哈希函数
                return std::hash<T>{}(obj);
            }
            else
            {
                // 驱动换成新运行时的哈希对象：旧驱动（`aggregateHasher`）随旧头一起退场，而
                // `hash` / `hashable` / 三个键类型是要保留、要搬进新头的，不能反过来依赖待删的头。
                // 两者对同一个宿主算出的值相同 —— 两条路的第一个分支都是"生成物给的特化"。
                codegen::hasher<hashAlg> hasher;
                hasher.hash(obj);
                return static_cast<result_type>(hasher.alg);
            }
        }
    };

    // 调换分派顺序的安全性判据 —— 对某个宿主，两个顺序选中同一条分支。
    //   * 调换前：平凡可拷贝优先（trivial ? 1 : custom ? 2 : 0）
    //   * 调换后：自定义特化优先（custom ? 2 : trivial ? 1 : 0）
    // 两条式子相等 ⇔ 这个宿主的哈希值不会因为调换而改变。写成一个 constexpr 判据，而不是叙述，
    // 因为"某个宿主其实可以平凡拷贝"这种事会在加成员 / 改拷贝构造时悄悄发生。
    // 默认的哈希对象是新运行时的那个 —— `hash<T>` 现在由它驱动，两者必须问同一套特化。
    template<typename Host, typename Hasher = codegen::hasher<>>
    constexpr bool dispatch_branch_unchanged() noexcept
    {
        constexpr bool trivial = std::is_trivially_copyable_v<Host>;
        constexpr bool custom  = codegen::has_custom_hash_func<Host, Hasher>;
        constexpr int  before  = trivial ? 1 : (custom ? 2 : 0);
        constexpr int  after   = custom ? 2 : (trivial ? 1 : 0);
        return before == after;
    }

} // namespace cacore

namespace std
{
    template<>
    struct hash <cacore::PathHash> {
        size_t operator()(const cacore::PathHash& obj) const {
            return obj.GetHash();
        }
    };

    template<>
    struct hash <cacore::NameHash > {
        size_t operator()(const cacore::NameHash& obj) const {
            return obj.GetHash();
        }
    };

    template<typename ObjType, cacore::EHashObjCompareMode CompareMode, typename hashAlg>
    struct hash <cacore::HashObj<ObjType, CompareMode, hashAlg>> {
        size_t operator()(const cacore::HashObj<ObjType, CompareMode, hashAlg>& obj) const {
            return obj.GetHash();
        }
    };
}

#define CANAME(str) cacore::NameHash::Static<castl::string_literal<castl::string_view{str}.size()>(str)>()
#define CAPATH(str) cacore::PathHash::Static<castl::string_literal<castl::string_view{str}.size()>(str)>()

constexpr auto format_as(cacore::NameHash const& nameHash) {
    return nameHash.c_str();
}

constexpr auto format_as(cacore::PathHash const& pathHash) {
	return pathHash.Get();
}

// 这里原先还有三份**旧命名空间**的托管包装规则（`HashObj` / `PathHash` / `NameHash` 各一份）：
// 那是**旧运行时**序列化"含这三个类型成员的结构体"时用的规则。旧头（`Reflection.h` 等三份）删掉之后
// 主模板不存在了，它们也没有使用者 —— 新运行时的对应物是上面 `namespace cacore::codegen` 里那几份
// `wrapper_traits`。

