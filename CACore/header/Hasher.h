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


    template<typename T, typename hasher>
    concept has_custom_hash_func = requires(T t, hasher h)
    {
        ca_hash(t, h);
    };

    template <typename hashAlg>
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
				ca_hash(object, *this);
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
            using objType = std::remove_reference_t<decltype(object)>;
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

    template<typename ObjType, typename hashAlg = fnv1a>
    struct HashObj
    {
    public:
        using result_type = hashAlg::result_type;
        HashObj(ObjType const& obj) : m_Object(obj)
        {
			UpdateHash();
		}
        constexpr result_type GetHash() const noexcept
		{
			return m_HashValue;
		}

        auto operator<=>(HashObj const& b) const
        {
            return m_HashValue <=> b.m_HashValue;
        }

        bool operator==(HashObj const& b) const
        {
            return m_HashValue == b.m_HashValue;
        }
    private:
        ObjType m_Object;
        result_type m_HashValue;
        void UpdateHash()
        {
            m_HashValue = hash<ObjType, hashAlg>{}(m_Object);
		}
    };
}

template<typename ObjType, typename hashAlg>
constexpr void ca_hash(cacore::HashObj<ObjType, hashAlg> const& obj, cacore::defaultHasher<hashAlg>& hasher)
{
    hasher.hash(obj.GetHash());
}