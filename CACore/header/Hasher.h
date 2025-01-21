#pragma once
#include "Reflection.h"
#include <CACore/CAHash.h>
#include <CASTL/CAFileSystem.h>
#include <CACore/CALiteralString.h>

namespace cacore
{
    using namespace careflection;

    using default_hashclass = typename cahash::komiHash;

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

    template<typename T>
    concept has_std_hash = requires(T t)
    {
        std::hash<T>{}(t);
    };

    template <typename hashAlg = default_hashclass>
    class defaultHasher
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

        template<typename Obj>
        constexpr void inline hash(const Obj& object)
        {
            using objType = std::remove_cvref_t<decltype(object)>;

			if constexpr (has_std_hash<objType>)
			{
				//使用标准哈希函数
				hash_range(std::hash<objType>{}(object));
			}
            else if constexpr (std::is_trivially_copyable_v<objType>)
            {
                //直接对对象内存算哈希值
                hash_range(object);
            }
            else if constexpr (has_custom_hash_func<objType, std::remove_cvref_t<decltype(*this)>>)
			{
				//自定义哈希函数
                custom_hash_trait<objType>::hash(object, *this);
				//ca_hash(object, *this);
			}
            else if constexpr (managed_pointer_traits<objType>::is_managed_pointer)
            {
                //对指针地址算哈希值
                hash_range(reinterpret_cast<uint64_t>(managed_pointer_traits<objType>::get_pointer(object)));
            }
            else if constexpr (std::is_pointer_v<objType>)
            {
                //对指针地址算哈希值
                hash_range(reinterpret_cast<uint64_t>(object));
            }
            else if constexpr (std::is_fundamental_v<objType> || std::is_enum_v<objType>)
            {
                hash_range(object);
            }
            else if constexpr (containerStates<objType>::is_container_with_size)
            {
                hash_container_with_size(object);
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
            hash_range(objSize);
            for (auto& item : object)
            {
                hash(item);
            }
        }

        template<typename Obj>
        constexpr void hash_range(const Obj& object)
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            static_assert(std::is_trivially_copyable_v<objType>, "Object must be trivially copyable");
            hash_range(&object, sizeof(objType));
        }

        constexpr void hash_range(const void* data, size_t size)
        {
            alg(data, size);
        }
    };

    template<typename T, typename hashAlg = default_hashclass>
    struct hash
    {
        using result_type = hashAlg::result_type;
        constexpr result_type operator()(const T& t) const noexcept
        {
            defaultHasher<hashAlg> hasher;
            hasher.hash(t);
            return static_cast<result_type>(hasher.alg);
        }
    };

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

        castl::weak_ordering operator<=>(HashObj const& b) const
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

        bool operator==(HashObj const& b) const
        {
            return m_HashValue == b.m_HashValue;
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

        friend struct careflection::managed_wrapper_traits<HashObj<ObjType, CompareMode, hashAlg>>;
    };

	struct PathHash : public HashObj<castl::string, EHashObjCompareMode::FullCompare, default_hashclass>
    {
    public:
        PathHash() = default;
		PathHash(const char* str) : HashObj(cafs::path(str).generic_string()) {}
        PathHash(castl::string const& str) : HashObj(cafs::path(str).generic_string()) {}
        PathHash(cafs::path const& path) : HashObj(path.generic_string()) {}
		operator castl::string() const noexcept
        {
            return Get();
        }
        friend struct careflection::managed_wrapper_traits<PathHash>;
    };

    //struct NameHash : public HashObj<castl::string, EHashObjCompareMode::FullCompare, default_hashclass>
    //{
    //public:
    //    NameHash() = default;
    //    NameHash(const char* str) : HashObj(str) {}
    //    NameHash(castl::string const& str) : HashObj(str) {}
    //    operator castl::string() const noexcept
    //    {
    //        return Get();
    //    }
    //};

    //template<typename hashAlg = default_hashclass>
    struct NameHash
    {
    public:
        using result_type = default_hashclass::result_type;
        using obj_type = castl::string;

        constexpr NameHash() : m_HashValue(0), m_HashValid(false), m_Name("") {}
        constexpr NameHash(const char* str) : m_Name(str)
        {
            m_NameView = castl::string_view(m_Name.c_str());
            UpdateHash();
        }
        constexpr NameHash(castl::string const& str) : m_Name(str)
        {
			m_NameView = castl::string_view(m_Name.c_str());
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
				m_NameView = castl::string_view(m_Name.c_str());
            }
        }

		constexpr castl::string string() const noexcept
		{
			return castl::string(m_NameView);
		}

        constexpr castl::string_view const& Get() const noexcept
        {
            return m_NameView;
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

        castl::weak_ordering operator<=>(NameHash const& b) const
        {
            return m_NameView <=> b.m_NameView;
        }

        bool operator==(NameHash const& b) const
        {
            return m_NameView == b.m_NameView;
        };

        constexpr void UpdateHash()
        {
            m_HashValue = hash<castl::string, default_hashclass>{}(m_Name);
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
        static constexpr NameHash StaticNameHash() {
            constexpr static std::size_t n = sizeof...(c);
            constexpr static const char data[n] = { c... };
            static const result_type hashVal = hash<const char[n], default_hashclass>{}(data);
            return NameHash(data, hashVal);
        };

        template <castl::string_literal str, size_t... N>
        static constexpr NameHash StaticNameHashInternal(castl::index_sequence<N...>) {
            return StaticNameHash<str.get_char<N>()...>();
        }

        template <castl::string_literal str>
        static constexpr NameHash Static() {
            return StaticNameHashInternal<str>(std::make_index_sequence<str.count>{});
        }

    private:
        result_type m_HashValue;
        bool m_HashValid;
        castl::string m_Name;
        castl::string_view m_NameView;
    };

    template<typename ObjType, EHashObjCompareMode CompareMode, typename hashAlg>
    struct custom_hash_trait<HashObj<ObjType, CompareMode, hashAlg>>
    {
        constexpr static void hash(HashObj<ObjType, CompareMode, hashAlg> const&obj, auto& hasher)
        {
            hasher.hash(obj.GetHash());
        }
    };
}

#define CANAME(str) cacore::NameHash::Static<castl::string_literal(str)>()

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
        using inner_type = cacore::PathHash::obj_type;
        constexpr static inner_type const& get_data(cacore::PathHash const& obj) { return obj.Get(); }
        constexpr static void set_data(cacore::PathHash& obj, inner_type const& data) { obj = cacore::PathHash{ data }; }
    };

    template<>
    struct managed_wrapper_traits<cacore::NameHash>
    {
        constexpr static bool is_managed_wrapper = true;
        using inner_type = castl::string;
        constexpr static castl::string const& get_data(cacore::NameHash const& obj) { return obj.string(); }
        constexpr static void set_data(cacore::NameHash& obj, castl::string const& data) { obj = cacore::NameHash{ data }; }
    };
}