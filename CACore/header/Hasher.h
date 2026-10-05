#pragma once
#include "Reflection.h"
#include <CACore/CAHash.h>
#include <CASTL/CAFileSystem.h>
#include <CACore/CALiteralString.h>
#include "CodeGenHashFwd.h"   // 过渡期：旧哈希设施里要用到新运行时的前置声明（特化检测）
// 三个哈希键类型（NameHash / PathHash / HashObj）还在这里：它们的 UpdateHash 走 `cacore::hash`
// → `aggregateHasher` → 老反射，而老驱动就住在下面 —— 两者现在分不开（分开时实测报
// `use of undefined type 'cacore::hash<...>'`）。等旧驱动随旧头一起删掉，再谈搬去新头。
#include <CACore/CAHash.h>
#include <CASTL/CAFileSystem.h>
#include <CASTL/CAString.h>
#include <CACore/CALiteralString.h>
#include "CodeGenHash.h"   // 需要 codegen::hasher 本体（不只是一句声明）：本文件的 hash 结构就用它算值

#include <type_traits>

namespace cacore
{

    using default_hashclass = typename cahash::constexpr_fnvla_64_Hash;

    template<typename T>
    concept has_std_hash = requires(T t)
    {
        castl::hash<T>{}(t);
    };

    // `cacore::hash<T, Alg>` 的定义在 `Hasher.h`（旧驱动那一侧）：非标注类型要靠它走老反射算哈希。
    // 这里只前置声明 —— 三个键类型的成员函数是模板，实例化时才需要看到定义，而那时消费方已经
    // include 了 `Hasher.h`。这样新头不必反过来依赖旧驱动。
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
}

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
    template<typename T>
    inline size_t hash_combine(size_t seed, T const& val)
    {
        seed ^= std::hash<T>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }

    using namespace careflection;


    template<typename T>
    struct custom_hash_trait
    {
        //constexpr void hash(T const&, hasher& h)
    };

    template<typename T, typename hasher>
    concept has_custom_hash_func = requires(T t, hasher h)
    {
        custom_hash_trait<T>::hash(t, h);
    };



    template <typename hashAlg = default_hashclass>
    class aggregateHasher
    {
    public:
        using result_type = typename hashAlg::result_type;
        hashAlg alg = {};

		result_type getHash() const noexcept
		{
			return static_cast<result_type>(alg);
		}

        result_type getHash() noexcept
        {
            return static_cast<result_type>(alg);
        }

        constexpr void hash_raw(const void* data, size_t size)
        {
            alg(data, size);
        }

        template<typename Obj>
        constexpr void hash_one(const Obj& object)
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            static_assert(std::is_trivially_copyable_v<objType>, "Object must be trivially copyable");
            hash_raw(&object, sizeof(objType));
        }

        template<typename Obj>
        constexpr void hash_range(const Obj* object, size_t size)
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            static_assert(std::is_trivially_copyable_v<objType>, "Object must be trivially copyable");
			size_t count = sizeof(objType) * size;
			//castl::cout << castl::to_string(count) << castl::endl;
			//castl::cout << castl::to_string(size) << castl::endl;
            hash_raw(object, count);
        }

        template<typename Obj>
        constexpr void inline hash(const Obj& object)
        {
            using objType = std::remove_cvref_t<decltype(object)>;


            // 分派顺序是「有自定义特化」优先于「平凡可拷贝」。
            // 原来"平凡可拷贝"排第一，纯 POD 类型永远轮不到自己的特化（不报错、静默不生效）；
            // 生成物提供的特化住在 cacore::codegen 里（新运行时），所以这里先问新运行时，再问
            // 旧设施里既有的那几个特化。两个顺序都不会改变既有宿主走哪条分支 —— 见下面
            // `dispatch_branch_unchanged` 与每个宿主自己那一行的编译期断言。
            if constexpr (codegen::has_custom_hash_func<objType, std::remove_cvref_t<decltype(*this)>>)
            {
                //生成物给出的特化（新运行时）
                codegen::custom_hash_trait<objType>::hash(object, *this);
            }
            else if constexpr (has_custom_hash_func<objType, std::remove_cvref_t<decltype(*this)>>)
			{
				//自定义哈希函数
                custom_hash_trait<objType>::hash(object, *this);
				//ca_hash(object, *this);
			}
            else if constexpr (std::is_trivially_copyable_v<objType>)
            {
                //直接对对象内存算哈希值
                hash_one(object);
            }
            else if constexpr (managed_pointer_traits<objType>::is_managed_pointer)
            {
                //对指针地址算哈希值
                hash_one(reinterpret_cast<uint64_t>(managed_pointer_traits<objType>::get_pointer(object)));
            }
            else if constexpr (std::is_pointer_v<objType>)
            {
                //对指针地址算哈希值
                hash_one(reinterpret_cast<uint64_t>(object));
            }
            else if constexpr (std::is_fundamental_v<objType> || std::is_enum_v<objType>)
            {
                hash_one(object);
            }
            else if constexpr (containerStates<objType>::is_container_with_size)
            {
                hash_container_with_size(object);
            }
            else if constexpr (has_std_hash<objType>)
            {
                //使用标准哈希函数
                hash_one(std::hash<objType>{}(object));
            }
            else if constexpr (std::is_class_v<objType>)
            {
                visit_members(object, [this](auto &&...items) CONSTEXPR_INLINE_LAMBDA{
                    (hash(items), ...);
                    }); //解包结构体
            }
        }
    private:

        template<typename Obj>
        constexpr void hash_container_with_size(const Obj& object)
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            using arrElemType = containerInfo<objType>::elementType;
            uint64_t objSize = containerInfo<objType>::container_size(object);
            hash_one(objSize);
            if constexpr (std::is_trivially_copyable_v<arrElemType> && containerStates<objType>::has_data)
            {
                hash_raw(object.data(), objSize * sizeof(arrElemType));
            }
            else
            {
                for (auto& item : object)
                {
                    hash(item);
                }
            }
        }

  
    };

    template<typename T, typename hashAlg = default_hashclass>
    concept hashable = requires (T const& obj)
    {
        aggregateHasher<hashAlg>{}.hash(obj);
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
                aggregateHasher<hashAlg> hasher;
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
    template<typename Host, typename Hasher = aggregateHasher<>>
    constexpr bool dispatch_branch_unchanged() noexcept
    {
        constexpr bool trivial = std::is_trivially_copyable_v<Host>;
        constexpr bool custom  = has_custom_hash_func<Host, Hasher>;
        constexpr int  before  = trivial ? 1 : (custom ? 2 : 0);
        constexpr int  after   = custom ? 2 : (trivial ? 1 : 0);
        return before == after;
    }



	//struct PathHash : public HashObj<castl::string, EHashObjCompareMode::FullCompare, default_hashclass>
 //   {
 //   public:
 //       PathHash() = default;
	//	PathHash(const char* str) : HashObj(cafs::path(str).generic_string()) {}
 //       PathHash(castl::string const& str) : HashObj(cafs::path(str).generic_string()) {}
 //       PathHash(cafs::path const& path) : HashObj(path.generic_string()) {}
 //       PathHash& operator=(PathHash const& other)
 //       {
 //           HashObj::operator=(other);
 //           return *this;
 //       }

 //       auto operator<=>(PathHash const& b) const
 //       {
 //           return HashObj::operator<=>(b);
 //       }

	//	operator castl::string() const noexcept
 //       {
 //           return Get();
 //       }
 //       friend struct careflection::managed_wrapper_traits<PathHash>;

 //       template <char... c>
 //       static constexpr PathHash const& StaticPathHash() {
 //           constexpr static std::size_t n = sizeof...(c);
 //           constexpr static const char data[n] = { c... };
 //           static const PathHash pathHash(data);
 //           return pathHash;
 //       };

 //       template <castl::string_literal str, size_t... N>
 //       static constexpr PathHash const& StaticPathHashInternal(castl::index_sequence<N...>) {
 //           return StaticPathHash<str.get_char<N>()...>();
 //       }

 //       template <castl::string_literal str>
 //       static constexpr PathHash const& Static() {
 //           return StaticPathHashInternal<str>(std::make_index_sequence<str.count>{});
 //       }

 //   };





    template<typename ObjType, EHashObjCompareMode CompareMode, typename hashAlg>
    struct custom_hash_trait<HashObj<ObjType, CompareMode, hashAlg>>
    {
        constexpr static void hash(HashObj<ObjType, CompareMode, hashAlg> const&obj, auto& hasher)
        {
            hasher.hash(obj.Get());
        }
    };

    template<>
    struct custom_hash_trait<NameHash>
    {
        constexpr static void hash(NameHash const& obj, auto& hasher)
        {
            hasher.hash_raw(obj.Get().data(), obj.Get().size());
        }
    };

    template<>
    struct custom_hash_trait<PathHash>
    {
        constexpr static void hash(PathHash const& obj, auto& hasher)
        {
            hasher.hash_raw(obj.Get().data(), obj.Get().size());
        }
    };

}

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

namespace careflection
{

    template<typename ObjType, cacore::EHashObjCompareMode CompareMode, typename hashAlg>
    struct managed_wrapper_traits<cacore::HashObj<ObjType, CompareMode, hashAlg>>
    {
        constexpr static bool is_managed_wrapper = true;
        using inner_type = ObjType;
        constexpr static ObjType const& get_data(cacore::HashObj<ObjType, CompareMode, hashAlg> const& obj) { return obj.Get(); }
        constexpr static void set_data(cacore::HashObj<ObjType, CompareMode, hashAlg>& obj, ObjType const& data) { obj = cacore::HashObj<ObjType, CompareMode, hashAlg>{ data }; }
    };

    template<>
    struct managed_wrapper_traits<cacore::PathHash>
    {
        constexpr static bool is_managed_wrapper = true;
        using inner_type = castl::string;
        constexpr static castl::string get_data(cacore::PathHash const& obj) { return obj.string(); }
        constexpr static void set_data(cacore::PathHash& obj, castl::string const& data) { obj = cacore::PathHash( data ); }
    };

    template<>
    struct managed_wrapper_traits<cacore::NameHash>
    {
        constexpr static bool is_managed_wrapper = true;
        using inner_type = castl::string;
        constexpr static castl::string get_data(cacore::NameHash const& obj) { return obj.string(); }
        constexpr static void set_data(cacore::NameHash& obj, castl::string const& data) { obj = cacore::NameHash{ data }; }
    };
}

// 新运行时（生成代码）要的规则：这三个类型各自包着什么、哈希怎么算。
// 与上面那份 `careflection::managed_wrapper_traits` 是同一件事，只是宿主换成新运行时；
// 旧头被删掉时上面那份随它消失，这一份留着。
