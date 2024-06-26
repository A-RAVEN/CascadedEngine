#pragma once
#include "Reflection.h"

namespace cacore
{
    using namespace careflection;

    class fnv1a
    {
        uint64_t state_ = 14695981039346656037u;
    public:
        using result_type = uint64_t;

        void operator()(void const* key, uint64_t len) noexcept
        {
            unsigned char const* p = static_cast<unsigned char const*>(key);
            unsigned char const* const e = p + len;
            for (; p < e; ++p)
                state_ = (state_ ^ *p) * 1099511628211u;
        }

        void operator()(result_type other) noexcept
        {
            operator()(&other, sizeof(result_type));
        }

        explicit
            operator result_type() const noexcept
        {
            return state_;
        }
    };


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

    template <typename hashAlg = fnv1a>
    class defaultHasher
    {
    public:
        using result_type = typename hashAlg::result_type;
        hashAlg alg = {};
        template<typename Obj>
        constexpr void inline hash(const Obj& object)
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            if constexpr (std::is_trivially_copyable_v<objType>)
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

    template<typename T, typename hashAlg = fnv1a>
    struct hash
    {
        using result_type = hashAlg::result_type;
        result_type operator()(const T& t) const noexcept
        {
            defaultHasher<hashAlg> hasher;
            hasher.hash(t);
            return static_cast<result_type>(hasher.alg);
        }
    };

    template<typename ObjType, bool FullCompare = false, typename hashAlg = fnv1a>
    struct HashObj
    {
    public:
        using result_type = hashAlg::result_type;
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

        castl::weak_ordering operator<=>(HashObj const& b) const
        {
            if constexpr (FullCompare)
				return m_Object <=> b.m_Object;
			else
                return m_HashValue <=> b.m_HashValue;
        }

        bool operator==(HashObj const& b) const
        {
            return m_HashValue == b.m_HashValue;
        }
    private:
        ObjType m_Object{};
        result_type m_HashValue{};
        bool m_HashValid = false;
        void UpdateHash()
        {
            m_HashValue = hash<ObjType, hashAlg>{}(m_Object);
            m_HashValid = true;
		}

        friend struct careflection::managed_wrapper_traits<HashObj<ObjType, FullCompare, hashAlg>>;
    };

    template<typename ObjType, bool FullCompare, typename hashAlg>
    struct custom_hash_trait<HashObj<ObjType, FullCompare, hashAlg>>
    {
        constexpr static void hash(HashObj<ObjType, FullCompare, hashAlg> const&obj, auto& hasher)
        {
            hasher.hash(obj.GetHash());
        }
    };
}

template<typename ObjType, bool FullCompare, typename hashAlg>
struct careflection::managed_wrapper_traits<cacore::HashObj<ObjType, FullCompare, hashAlg>>
{
    constexpr static bool is_managed_wrapper = true;
    using inner_type = ObjType;
    constexpr static ObjType const& get_data(cacore::HashObj<ObjType, FullCompare, hashAlg> const& obj) { return obj.Get(); }
    constexpr static void set_data(cacore::HashObj<ObjType, FullCompare, hashAlg>& obj, ObjType const& data) { obj = cacore::HashObj<ObjType, FullCompare, hashAlg>{ data }; }
};